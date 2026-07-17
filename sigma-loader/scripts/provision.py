#!/usr/bin/env python3
"""
RiskNox provisioner core.
Creates the index, discovers pre-packaged Windows Sigma rule IDs, and creates
the Security Analytics detector WITH auto-generated field mappings.

Auto-mapping strategy:
OpenSearch Security Analytics provides predefined, out-of-the-box mappings for Windows logs.
When we create a detector for the "windows" type, the system automatically maps fields.
"""
import sys, json, time, urllib.request, urllib.error

BASE          = sys.argv[1] if len(sys.argv) > 1 else "http://opensearch:9200"
INDEX_NAME    = sys.argv[2] if len(sys.argv) > 2 else "rp-events"
DETECTOR_NAME = sys.argv[3] if len(sys.argv) > 3 else "Windows-Threat-Detector"
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
# 1. Ensure index exists (inherits template mapping)
# ─────────────────────────────────────────────
def ensure_index():
    chk, _ = req("GET", f"/{INDEX_NAME}")
    if chk and "error" not in chk:
        print(f"[1/3] Index '{INDEX_NAME}' already exists.", flush=True)
        return
    print(f"[1/3] Creating index '{INDEX_NAME}' with seed mapping...", flush=True)

    # Seed document so OpenSearch creates index.
    seed = {
        "timestamp": "2000-01-01T00:00:00Z",
        "@timestamp": "2000-01-01T00:00:00Z",
        "_init": True,
    }

    r, e = req("POST", f"/{INDEX_NAME}/_doc", seed)
    if r and r.get("result") == "created":
        req("DELETE", f"/{INDEX_NAME}/_doc/{r.get('_id')}", timeout=15)
        print(f"[1/3] Index created and mapping established.", flush=True)
    elif e:
        print(f"[1/3] Seed warning: {e}", flush=True)
    time.sleep(2)



# ─────────────────────────────────────────────
# 2. Discover pre-packaged Windows rule IDs
# ─────────────────────────────────────────────
def discover_windows_rule_ids():
    total_res, err = req("POST",
        "/_plugins/_security_analytics/rules/_search?pre_packaged=true",
        {"query": {"match_all": {}}, "size": 0})
    if not total_res:
        print(f"[2/3] ERROR querying rules: {err}", flush=True)
        return []
    total = total_res.get("hits", {}).get("total", {}).get("value", 0)
    print(f"[2/3] Pre-packaged rules available: {total}", flush=True)

    ids, fetched = [], 0
    while fetched < total:
        res, err = req("POST",
            "/_plugins/_security_analytics/rules/_search?pre_packaged=true",
            {"query": {"match_all": {}}, "size": 500, "from": fetched}, timeout=90)
        if not res:
            print(f"[2/3] page error @ {fetched}: {err}", flush=True)
            break
        hits = res.get("hits", {}).get("hits", [])
        if not hits:
            break
        for h in hits:
            if h.get("_source", {}).get("category") == "windows":
                ids.append(h["_id"])
        fetched += len(hits)
    print(f"[2/3] Windows-category rule IDs: {len(ids)}", flush=True)
    return ids





# ─────────────────────────────────────────────
# 4. Create detector (idempotent)
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
        print(f"[3/3] Detector exists (monitor={existing['monitor_id']}) — skipping.", flush=True)
        return existing["monitor_id"]

    if not rule_ids:
        print("[3/3] No rule IDs — cannot create detector.", flush=True)
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
        }, {
            "name": "Low Severity Alert",
            "severity": "4",
            "types": ["windows"],
            "ids": [],
            "sev_levels": ["medium", "low"],
            "tags": [],
            "actions": [],
        }],
    }

    print(f"[3/3] Creating detector with {len(rule_ids)} rules (may take 60-120s)...", flush=True)
    res, err = req("POST", "/_plugins/_security_analytics/detectors", payload, timeout=300)
    if not (res and res.get("_id")):
        print(f"[3/3] Detector creation failed: {err}", flush=True)
        return ""
    det_id = res["_id"]
    # monitor_id is only available via _search after creation
    time.sleep(3)
    for _ in range(10):
        ex = get_existing_detector()
        if ex and ex["monitor_id"]:
            print(f"[3/3] Detector created: id={det_id} monitor={ex['monitor_id']}", flush=True)
            return ex["monitor_id"]
        time.sleep(3)
    print(f"[3/3] Detector created (id={det_id}) but monitor_id not resolved yet.", flush=True)
    return ""


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────
def main():
    ensure_index()
    rule_ids = discover_windows_rule_ids()
    monitor_id = create_detector(rule_ids)
    if monitor_id:
        print(f"Provisioning complete. Detector monitor_id={monitor_id}", flush=True)
    else:
        print("Provisioning complete (no monitor_id resolved).", flush=True)

    # Keep container alive for health checks
    print("Provisioner entering idle loop.", flush=True)
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()

