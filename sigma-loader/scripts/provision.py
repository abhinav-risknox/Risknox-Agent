#!/usr/bin/env python3
"""
RiskNox OSSA provisioner (replaces sigma-loader's manual sigma convert).
Creates the rp-events index, the Security Analytics detector with real
pre-packaged Windows rule IDs, and ONLY the suffixed field aliases that the
detector's compiled Sigma queries reference.

Adapted from the export's provisioner — proven to produce ~1,580 compiled rules.
"""
import sys, json, time, urllib.request, urllib.error, os

BASE          = os.environ.get("OPENSEARCH_URL", sys.argv[1] if len(sys.argv) > 1 else "http://opensearch:9200")
INDEX_NAME    = os.environ.get("INDEX_NAME", sys.argv[2] if len(sys.argv) > 2 else "rp-events")
DETECTOR_NAME = os.environ.get("DETECTOR_NAME", sys.argv[3] if len(sys.argv) > 3 else "Windows-Threat-Detector")
HEADERS = {"Content-Type": "application/json", "Accept": "application/json"}


def req(method, path, data=None, timeout=180):
    body = json.dumps(data).encode() if data is not None else None
    r = urllib.request.Request(BASE + path, data=body, headers=HEADERS, method=method)
    try:
        resp = urllib.request.urlopen(r, timeout=timeout)
        return json.loads(resp.read()), None
    except urllib.error.HTTPError as e:
        return None, f"HTTP {e.code}: {e.read().decode()[:400]}"
    except Exception as ex:
        return None, str(ex)


# ─────────────────────────────────────────────
# 3. Ensure index exists (inherits template mapping)
# ─────────────────────────────────────────────
def ensure_index():
    chk, _ = req("GET", f"/{INDEX_NAME}")
    if chk and "error" not in chk:
        print(f"[3/6] Index '{INDEX_NAME}' already exists.", flush=True)
        return
    print(f"[3/6] Creating index '{INDEX_NAME}'...", flush=True)
    r, e = req("POST", f"/{INDEX_NAME}/_doc", {
        "timestamp": "2000-01-01T00:00:00Z",
        "@timestamp": "2000-01-01T00:00:00Z",
        "_init": True,
        "winlog": {"event_id": 0, "event_data": {}, "channel": "init", "provider_name": "init"},
    })
    if r and r.get("result") == "created":
        req("DELETE", f"/{INDEX_NAME}/_doc/{r.get('_id')}", timeout=15)
    time.sleep(2)


# ─────────────────────────────────────────────
# 3b. ECS base aliases (CRITICAL for rule compilation)
#     OSSA's Windows Sigma rules map most Sigma fields to ECS names
#     (process.command_line, process.executable, source.ip, ...). These must
#     exist in the mapping BEFORE the detector is created, otherwise those
#     rules silently fail to compile (~760 vs ~1290 compiled).
#
#     We point them at the agent's real winlog.event_data.* fields.
# ─────────────────────────────────────────────
ECS_ALIASES = {
    "process": {"properties": {
        "command_line": {"type": "alias", "path": "winlog.event_data.CommandLine"},
        "executable":   {"type": "alias", "path": "winlog.event_data.Image"},
        "parent": {"properties": {
            "executable":   {"type": "alias", "path": "winlog.event_data.ParentImage"},
            "command_line": {"type": "alias", "path": "winlog.event_data.ParentCommandLine"},
        }},
    }},
    "source": {"properties": {
        "ip":   {"type": "alias", "path": "winlog.event_data.SourceIp"},
        "port": {"type": "alias", "path": "winlog.event_data.SourcePort"},
    }},
    "destination": {"properties": {
        "ip":     {"type": "alias", "path": "winlog.event_data.DestinationIp"},
        "port":   {"type": "alias", "path": "winlog.event_data.DestinationPort"},
        "domain": {"type": "alias", "path": "winlog.event_data.DestinationHostname"},
    }},
}


def ensure_ecs_aliases():
    r, err = req("PUT", f"/{INDEX_NAME}/_mapping", {"properties": ECS_ALIASES})
    if r and r.get("acknowledged"):
        print("[3b/6] ECS base aliases applied (enables ECS-mapped Sigma rules).", flush=True)
    else:
        print(f"[3b/6] ECS alias warning: {err}", flush=True)


# ─────────────────────────────────────────────
# 4. Discover pre-packaged Windows rule IDs
# ─────────────────────────────────────────────
def discover_windows_rule_ids():
    total_res, err = req("POST",
        "/_plugins/_security_analytics/rules/_search?pre_packaged=true",
        {"query": {"match_all": {}}, "size": 0})
    if not total_res:
        print(f"[4/6] ERROR querying rules: {err}", flush=True)
        return []
    total = total_res.get("hits", {}).get("total", {}).get("value", 0)
    print(f"[4/6] Pre-packaged rules available: {total}", flush=True)

    ids, fetched = [], 0
    while fetched < total:
        res, err = req("POST",
            "/_plugins/_security_analytics/rules/_search?pre_packaged=true",
            {"query": {"match_all": {}}, "size": 500, "from": fetched}, timeout=90)
        if not res:
            print(f"[4/6] page error @ {fetched}: {err}", flush=True)
            break
        hits = res.get("hits", {}).get("hits", [])
        if not hits:
            break
        for h in hits:
            if h.get("_source", {}).get("category") == "windows":
                ids.append(h["_id"])
        fetched += len(hits)
    print(f"[4/6] Windows-category rule IDs: {len(ids)}", flush=True)
    return ids


# ─────────────────────────────────────────────
# 5. Create detector (idempotent)
# ─────────────────────────────────────────────
def get_existing_detector():
    res, _ = req("POST", "/_plugins/_security_analytics/detectors/_search",
                 {"query": {"match_all": {}}})
    if not res:
        return None
    for h in res.get("hits", {}).get("hits", []):
        src = h.get("_source", {})
        if src.get("name") == DETECTOR_NAME:
            mid = src.get("monitor_id", [])
            mid = mid[0] if isinstance(mid, list) and mid else ""
            return {"id": h["_id"], "monitor_id": mid}
    return None


def create_detector(rule_ids):
    existing = get_existing_detector()
    if existing and existing["monitor_id"]:
        print(f"[5/6] Detector exists (monitor={existing['monitor_id']}) — skipping.", flush=True)
        return existing["monitor_id"]

    if not rule_ids:
        print("[5/6] No rule IDs — cannot create detector.", flush=True)
        return ""

    payload = {
        "name": DETECTOR_NAME,
        "detector_type": "windows",
        "enabled": True,
        "inputs": [{
            "detector_input": {
                "description": "RiskNox Windows endpoint detection",
                "indices": [INDEX_NAME],
                "pre_packaged_rules": [{"id": r} for r in rule_ids],
                "custom_rules": [],
            }
        }],
        "schedule": {"period": {"interval": 1, "unit": "MINUTES"}},
        "triggers": [{
            "name": "High Severity Alert",
            "severity": "1",
            "types": ["windows"],
            "ids": [],
            "sev_levels": ["critical", "high"],
            "tags": [],
            "actions": [],
        }],
    }
    print(f"[5/6] Creating detector with {len(rule_ids)} rules (may take 60-120s)...", flush=True)
    res, err = req("POST", "/_plugins/_security_analytics/detectors", payload, timeout=300)
    if not (res and res.get("_id")):
        print(f"[5/6] Detector creation failed: {err}", flush=True)
        return ""
    det_id = res["_id"]
    # monitor_id is only available via _search after creation
    time.sleep(3)
    for _ in range(10):
        ex = get_existing_detector()
        if ex and ex["monitor_id"]:
            print(f"[5/6] Detector created: id={det_id} monitor={ex['monitor_id']}", flush=True)
            return ex["monitor_id"]
        time.sleep(3)
    print(f"[5/6] Detector created (id={det_id}) but monitor_id not resolved yet.", flush=True)
    return ""


# ─────────────────────────────────────────────
# 6. Suffix-only field aliases
# ─────────────────────────────────────────────
# Sigma field (under winlog.event_data) -> real path. Real path always equals
# the field itself; we only build <leaf>_<index>_<monitor> aliases.
EVENT_DATA_FIELDS = [
    "Image", "CommandLine", "ParentImage", "ParentCommandLine", "OriginalFileName",
    "User", "IntegrityLevel", "CurrentDirectory", "ProcessGuid", "ParentProcessGuid",
    "ProcessId", "ParentProcessId", "Company", "Description", "Product", "FileVersion",
    "Hashes", "DestinationIp", "DestinationPort", "DestinationHostname", "SourceIp",
    "SourcePort", "Protocol", "Initiated", "ImageLoaded", "Signed", "Signature",
    "SignatureStatus", "TargetObject", "Details", "EventType", "TargetFilename",
    "GrantedAccess", "SourceImage", "TargetImage", "CallTrace", "PipeName", "QueryName",
    "QueryResults", "QueryStatus", "RuleName", "ServiceName", "ServiceFileName",
    "StartType", "ScriptBlockText", "HostApplication", "Path", "SubjectUserName",
    "SubjectDomainName", "SubjectUserSid", "TargetUserName", "TargetDomainName",
    "LogonType", "LogonProcessName", "AuthenticationPackageName", "IpAddress", "IpPort",
    "WorkstationName", "ProcessName", "NewProcessName", "NewProcessId", "AccountName",
    "AccountDomain", "ObjectName", "ObjectType", "TaskName", "TaskContent", "StartAddress",
    "StartModule", "StartFunction", "TicketOptions", "TicketEncryptionType",
]


def flatten(props, prefix=""):
    out = set()
    for k, v in props.items():
        full = f"{prefix}.{k}" if prefix else k
        t = v.get("type", "")
        if t and t not in ("object", "nested", "alias"):
            out.add(full)
        if "properties" in v:
            out |= flatten(v["properties"], full)
    return out


def existing_fields():
    m, _ = req("GET", f"/{INDEX_NAME}/_mapping")
    if not m:
        return set()
    props = m.get(INDEX_NAME, {}).get("mappings", {}).get("properties", {})
    return flatten(props)


def apply_aliases(suffix, present):
    edata = {}
    for f in EVENT_DATA_FIELDS:
        real = f"winlog.event_data.{f}"
        if real in present:
            edata[f"{f}{suffix}"] = {"type": "alias", "path": real}

    winlog_props = {"event_data": {"properties": edata}}
    for f, real in [("event_id", "winlog.event_id"),
                    ("channel", "winlog.channel"),
                    ("provider_name", "winlog.provider_name")]:
        if real in present:
            winlog_props[f"{f}{suffix}"] = {"type": "alias", "path": real}

    proc_props = {}
    if "winlog.event_data.CommandLine" in present:
        proc_props[f"command_line{suffix}"] = {"type": "alias", "path": "winlog.event_data.CommandLine"}
    if "winlog.event_data.Image" in present:
        proc_props[f"executable{suffix}"] = {"type": "alias", "path": "winlog.event_data.Image"}
    if "winlog.event_data.ParentImage" in present:
        proc_props["parent"] = {"properties": {f"executable{suffix}": {"type": "alias", "path": "winlog.event_data.ParentImage"}}}

    body = {"properties": {"winlog": {"properties": winlog_props}}}
    if proc_props:
        body["properties"]["process"] = {"properties": proc_props}

    r, err = req("PUT", f"/{INDEX_NAME}/_mapping", body)
    if r and r.get("acknowledged"):
        return len(edata)
    print(f"[6/6] alias apply warning: {err}", flush=True)
    return 0


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────
def main():
    ensure_index()
    ensure_ecs_aliases()
    rule_ids = discover_windows_rule_ids()
    monitor_id = create_detector(rule_ids)
    if not monitor_id:
        print("No monitor_id — alias step skipped. Provisioner will idle.", flush=True)
    else:
        suffix = f"_{INDEX_NAME}_{monitor_id}"
        print(f"[6/6] Applying suffixed aliases (suffix={suffix})...", flush=True)
        present = existing_fields()
        n = apply_aliases(suffix, present)
        print(f"[6/6] Applied {n} event_data aliases.", flush=True)

        # 7. maintenance loop
        print("Provisioner entering maintenance loop (keeps container alive).", flush=True)
        seen = set(present)
        for i in range(100000):
            time.sleep(60)
            try:
                now = existing_fields()
                if now - seen:
                    n = apply_aliases(suffix, now)
                    print(f"[maint] new fields -> re-applied {n} aliases.", flush=True)
                    seen = now
            except Exception as ex:
                print(f"[maint] error: {ex}", flush=True)
        return

    # idle so the container stays up for inspection
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
