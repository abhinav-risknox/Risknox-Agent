#!/usr/bin/env python3
"""
RiskNox provisioner core.
Creates the index, discovers pre-packaged Windows Sigma rule IDs, and creates
the Security Analytics detector WITH auto-generated field mappings.

Auto-mapping strategy:
  1. Query OSSA for the complete list of detection rule fields for log type "windows"
  2. For each rule field (e.g. "winlog.event_data.Image"), derive the raw_field name
     that Data Prepper writes to the index (e.g. "Image")
  3. Include these mappings explicitly in the detector creation payload
  4. No manual template aliases needed — OSSA uses the mappings we provide

This eliminates the need for maintaining alias mappings in template.json.
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


# ─────────────────────────────────────────────────────────────────────────────
# Detection rule field → index raw_field mapping
# ─────────────────────────────────────────────────────────────────────────────
# Maps every detection rule field (left) to the flat PascalCase field name
# that Data Prepper's windows_xml_parser writes to the index (right).
#
# The parser extracts XML <Data Name="X"> values and writes them flat at the
# document root using the exact PascalCase name from the XML.
# ─────────────────────────────────────────────────────────────────────────────

FIELD_MAPPINGS = {
    # ── Existing mapped fields (48) ───────────────────────────────────────────
    "winlog.event_id":                          "EventID",
    "winlog.provider_name":                     "Provider_Name",
    "winlog.channel":                           "Channel",
    "winlog.computer_name":                     "ComputerName",
    "winlog.computerObject.name":               "ObjectName",
    "winlog.keywords":                          "Keywords",
    "winlog.event_data.SubjectUserSid":         "SubjectUserSid",
    "winlog.event_data.SubjectUserName":        "SubjectUserName",
    "winlog.event_data.SubjectDomainName":      "SubjectDomainName",
    "winlog.event_data.SubjectLogonId":         "SubjectLogonId",
    "winlog.event_data.AuthenticationPackageName": "AuthenticationPackageName",
    "winlog.event_data.SidHistory":             "SidHistory",
    "winlog.event_data.PrivilegeList":          "PrivilegeList",
    "winlog.event_data.IpAddress":              "IpAddress",
    "winlog.event_data.TargetOutboundUserName": "TargetOutboundUserName",
    "winlog.event_data.ServiceType":            "ServiceType",
    "winlog.event_data.SamAccountName":         "SamAccountName",
    "winlog.event_data.KeyLength":              "KeyLength",
    "winlog.event_data.ScriptBlockText":        "ScriptBlockText",
    "winlog.event_data.Status":                 "Status",
    "winlog.event_data.TargetSid":              "TargetSid",
    "winlog.event_data.TargetLogonId":          "TargetLogonId",
    "winlog.event_data.NewUacValue":            "NewUacValue",
    "winlog.event_data.AllowedToDelegateTo":    "AllowedToDelegateTo",
    "winlog.event_data.ProcessId":              "ProcessId",
    "winlog.event_data.ImpersonationLevel":     "ImpersonationLevel",
    "winlog.event_data.OldUacValue":            "OldUacValue",
    "winlog.event_data.TargetUserName":         "TargetUserName",
    "winlog.event_data.TargetServerName":       "TargetServerName",
    "winlog.event_data.TargetUserSid":          "TargetUserSid",
    "winlog.event_data.LogonProcessName":       "LogonProcessName",
    "winlog.event_data.param1":                 "param1",
    "winlog.event_data.param2":                 "param2",
    "winlog.event_data.param3":                 "param3",
    "winlog.event_data.Level":                  "Level",
    "winlog.event_data.Workstation":            "WorkstationName",
    "winlog.event_data.LogonType":              "LogonType",
    "winlog.event_data.ImagePath":              "ImagePath",
    "winlog.event_data.Path":                   "Path",
    "process.command_line":                     "CommandLine",
    "winlog.event_data.PasswordLastSet":        "PasswordLastSet",
    "winlog.event_data.State":                  "State",
    "winlog.event_data.ProcessName":            "ProcessName",
    "winlog.event_data.ServiceName":            "ServiceName",

    # ── Previously unmapped fields (103) ──────────────────────────────────────

    # Sysmon Process Creation (EID 1)
    "winlog.event_data.Image":                  "Image",
    "winlog.event_data.ParentImage":            "ParentImage",
    "winlog.event_data.ParentCommandLine":      "ParentCommandLine",
    "winlog.event_data.ParentProcessId":        "ParentProcessId",
    "winlog.event_data.ParentUser":             "ParentUser",
    "winlog.event_data.CurrentDirectory":       "CurrentDirectory",
    "winlog.event_data.IntegrityLevel":         "IntegrityLevel",
    "winlog.event_data.Hashes":                 "Hashes",
    "winlog.event_data.Hash":                   "Hash",
    "winlog.event_data.OriginalFileName":       "OriginalFilename",
    "winlog.event_data.OriginalName":           "OriginalName",
    "winlog.event_data.Company":                "Company",
    "winlog.event_data.Product":                "Product",
    "winlog.event_data.FileVersion":            "FileVersion",
    "winlog.event_data.Description":            "Description",
    "winlog.event_data.LogonId":                "LogonId",
    "winlog.event_data.Commandline":            "Commandline",

    # Sysmon File Events (EID 2, 11, 15, 23, 26)
    "winlog.event_data.TargetFilename":         "TargetFilename",
    "winlog.event_data.CreationUtcTime":        "CreationUtcTime",
    "winlog.event_data.PreviousCreationUtcTime":"PreviousCreationUtcTime",

    # Sysmon Network Connection (EID 3)
    "winlog.event_data.DestinationHostname":    "DestinationHostname",
    "winlog.event_data.DestinationIsIpv6":      "DestinationIsIpv6",
    "winlog.event_data.DestAddress":            "DestAddress",
    "winlog.event_data.Protocol":               "Protocol",
    "winlog.event_data.Initiated":              "Initiated",
    "winlog.event_data.SourceHostname":         "SourceHostname",
    "destination.ip":                           "DestinationIp",
    "destination.port":                         "DestPort",
    "source.ip":                                "SourceAddress",
    "source.port":                              "SourcePort",

    # Sysmon Registry (EID 12, 13, 14)
    "winlog.event_data.TargetObject":           "TargetObject",
    "winlog.event_data.Detail":                 "Details",
    "winlog.event_data.NewValue":               "NewValue",
    "winlog.event_data.OldValue":               "OldValue",
    "winlog.event_data.EventType":              "EventType",

    # Sysmon Image/Driver Load (EID 6, 7)
    "winlog.event_data.ImageLoaded":            "ImageLoaded",
    "winlog.event_data.Signature":              "Signature",
    "winlog.event_data.SignatureStatus":         "SignatureStatus",
    "winlog.event_data.Signed":                 "Signed",
    "winlog.event_data.Imphash":                "Imphash",
    "winlog.event_data.CertThumbprint":         "CertThumbprint",

    # Sysmon CreateRemoteThread (EID 8)
    "winlog.event_data.StartAddress":           "StartAddress",
    "winlog.event_data.StartModule":            "StartModule",
    "winlog.event_data.StartFunction":          "StartFunction",

    # Sysmon Process Access (EID 10)
    "winlog.event_data.SourceImage":            "SourceImage",
    "winlog.event_data.TargetImage":            "TargetImage",
    "winlog.event_data.GrantedAccess":          "GrantedAccess",
    "winlog.event_data.CallTrace":              "CallTrace",

    # Sysmon Named Pipe (EID 17, 18)
    "winlog.event_data.PipeName":               "PipeName",

    # Sysmon WMI (EID 19, 20, 21)
    "winlog.event_data.Name":                   "Name",
    "winlog.event_data.Destination":            "Destination",
    "winlog.event_data.Query":                  "Query",

    # Sysmon DNS Query (EID 22)
    "winlog.event_data.QueryName":              "QueryName",
    "winlog.event_data.QueryResults":           "QueryResults",
    "winlog.event_data.QueryStatus":            "QueryStatus",
    "winlog.event_data.QNAME":                  "QNAME",

    # Object Access / Audit
    "winlog.event_data.Accesses":               "Accesses",
    "winlog.event_data.AccessMask":             "AccessMask",
    "winlog.event_data.AccessList":             "AccessList",
    "winlog.event_data.ObjectType":             "ObjectType",
    "winlog.event_data.ObjectServer":           "ObjectServer",
    "winlog.event_data.ObjectClass":            "ObjectClass",
    "winlog.event_data.ObjectValueName":        "ObjectValueName",

    # File Share
    "winlog.event_data.ShareName":              "ShareName",
    "winlog.event_data.RelativeTargetName":     "RelativeTargetName",

    # Firewall
    "winlog.event_data.Application":            "Application",
    "winlog.event_data.ApplicationPath":        "ApplicationPath",
    "winlog.event_data.Action":                 "Action",
    "winlog.event_data.FilterOrigin":           "FilterOrigin",
    "winlog.event_data.LayerRTID":              "LayerRTID",
    "winlog.event_data.RemoteAddress":          "RemoteAddress",
    "winlog.event_data.RemoteName":             "RemoteName",
    "winlog.event_data.TargetPort":             "TargetPort",

    # Kerberos
    "winlog.event_data.TicketEncryptionType":   "TicketEncryptionType",
    "winlog.event_data.TicketOptions":          "TicketOptions",
    "winlog.event_data.FailureCode":            "FailureCode",
    "winlog.event_data.ErrorCode":              "ErrorCode",

    # Audit Policy
    "winlog.event_data.AuditPolicyChanges":     "AuditPolicyChanges",
    "winlog.event_data.AuditSourceName":        "AuditSourceName",

    # PowerShell (4103, 4104)
    "winlog.event_data.EngineVersion":          "EngineVersion",
    "winlog.event_data.HostVersion":            "HostVersion",
    "winlog.event_data.HostApplication":        "HostApplication",
    "winlog.event_data.ContextInfo":            "ContextInfo",
    "winlog.event_data.Payload":                "Payload",
    "winlog.event_data.ScriptBlockLogging":     "ScriptBlockLogging",

    # Misc event_data fields
    "winlog.event_data.CallerProcessName":      "CallerProcessName",
    "winlog.event_data.ClassName":              "ClassName",
    "winlog.event_data.DeviceDescription":      "DeviceDescription",
    "winlog.event_data.DeviceName":             "DeviceName",
    "winlog.event_data.Device":                 "Device",
    "winlog.event_data.ImageFileName":          "ImageFileName",
    "winlog.event_data.OldTargetUserName":      "OldTargetUserName",
    "winlog.event_data.NewTargetUserName":       "NewTargetUserName",
    "winlog.event_data.Source_Name":            "Source_Name",
    "winlog.event_data.SourceName":             "SourceName",
    "winlog.event_data.Message":                "Message",

    "winlog.event_data.Address":                "Address",
    "winlog.event_data.AppID":                  "AppID",
    "winlog.event_data.AppName":                "AppName",
    "winlog.event_data.AttributeLDAPDisplayName":"AttributeLDAPDisplayName",
    "winlog.event_data.AttributeValue":         "AttributeValue",
    "winlog.event_data.Caption":                "Caption",
    "winlog.event_data.ClientProcessId":        "ClientProcessId",
    "winlog.event_data.Contents":               "Contents",
    "winlog.event_data.Data":                   "Data",
    "winlog.event_data.ExceptionCode":          "ExceptionCode",
    "winlog.event_data.FileName":               "FileName",
    "winlog.event_data.FileNameBuffer":         "FileNameBuffer",
    "winlog.event_data.HiveName":               "HiveName",
    "winlog.event_data.ImageName":              "ImageName",
    "winlog.event_data.LocalName":              "LocalName",
    "winlog.event_data.ModifyingApplication":   "ModifyingApplication",
    "winlog.event_data.NewName":                "NewName",
    "winlog.event_data.NewTemplateContent":     "NewTemplateContent",
    "winlog.event_data.Origin":                 "Origin",
    "winlog.event_data.PackageFullName":        "PackageFullName",
    "winlog.event_data.PackagePath":            "PackagePath",
    "winlog.event_data.PossibleCause":          "PossibleCause",
    "winlog.event_data.ProcessNameBuffer":      "ProcessNameBuffer",
    "winlog.event_data.ProcessPath":            "ProcessPath",
    "winlog.event_data.Properties":             "Properties",
    "winlog.event_data.RequestedPolicy":        "RequestedPolicy",
    "winlog.event_data.SearchFilter":           "SearchFilter",
    "winlog.event_data.ServiceFileName":        "ServiceFileName",
    "winlog.event_data.ServicePrincipalNames":  "ServicePrincipalNames",
    "winlog.event_data.ServiceStartType":       "ServiceStartType",
    "winlog.event_data.SidList":                "SidList",
    "winlog.event_data.SourceCommandLine":      "SourceCommandLine",
    "winlog.event_data.SourceFilename":         "SourceFilename",
    "winlog.event_data.SourceParentImage":      "SourceParentImage",
    "winlog.event_data.TargetParentImage":      "TargetParentImage",
    "winlog.event_data.TargetParentProcessId":  "TargetParentProcessId",
    "winlog.event_data.TaskContent":            "TaskContent",
    "winlog.event_data.TaskContentNew":         "TaskContentNew",
    "winlog.event_data.TemplateContent":        "TemplateContent",
    "winlog.event_data.ValidatedPolicy":        "ValidatedPolicy",
    "winlog.event_data.Value":                  "Value",
    "winlog.event_data.subjectName":            "subjectName",
    "winlog.event_data.process":                "process",
    "winlog.event_data.payload":                "payload",

    # ECS-level aliases
    "host.hostname":                            "hostname",
    "host.name":                                "HostName",
    "hash.sha1":                                "sha1",
    "hash.md5":                                 "md5",
    "hash.sha256":                              "sha256",
    "windows.message":                          "message",
    "winlog.user.name":                         "UserName",
    "winlog.user.type":                         "Type",
    "winlog.task":                              "TaskName",
    "timestamp":                                "creationTime",
}


# ─────────────────────────────────────────────
# 1. Ensure index exists (inherits template mapping)
# ─────────────────────────────────────────────
def ensure_index():
    chk, _ = req("GET", f"/{INDEX_NAME}")
    if chk and "error" not in chk:
        print(f"[1/3] Index '{INDEX_NAME}' already exists.", flush=True)
        return
    print(f"[1/3] Creating index '{INDEX_NAME}' with seed mapping...", flush=True)

    # Seed document containing ALL raw_field names so OpenSearch creates
    # keyword mappings before the detector reads the index mapping.
    seed = {
        "timestamp": "2000-01-01T00:00:00Z",
        "@timestamp": "2000-01-01T00:00:00Z",
        "_init": True,
    }
    # Auto-generate seed from the field mappings — every unique raw_field
    # gets a seed entry. No manual list to maintain.
    for raw_field in set(FIELD_MAPPINGS.values()):
        seed[raw_field] = "_init"

    r, e = req("POST", f"/{INDEX_NAME}/_doc", seed)
    if r and r.get("result") == "created":
        req("DELETE", f"/{INDEX_NAME}/_doc/{r.get('_id')}", timeout=15)
        print(f"[1/3] Index created, {len(seed)-3} raw_fields seeded and mapping established.", flush=True)
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
# 3. Query OSSA for unmapped rule fields and
#    apply mappings only for recognized fields
# ─────────────────────────────────────────────
def get_index_fields():
    """Return the set of concrete field names in the index mapping."""
    res, err = req("GET", f"/{INDEX_NAME}/_mapping")
    if not res:
        print(f"  -> Could not read index mapping: {err}", flush=True)
        return set()
    # Navigate: { "rp-events": { "mappings": { "properties": { "Image": {...}, ... } } } }
    for idx_name, idx_data in res.items():
        props = idx_data.get("mappings", {}).get("properties", {})
        return set(props.keys())
    return set()


def apply_field_mappings():
    """
    Query OSSA for unmapped detection rule fields, cross-reference with
    FIELD_MAPPINGS, and apply only valid mappings.
    
    Returns the number of successfully applied mappings.
    """
    # 1. Get the OSSA mapping view to find what's unmapped
    res, err = req("GET", f"/_plugins/_security_analytics/mappings/view?index_name={INDEX_NAME}&rule_topic=windows")
    if not res or "response" not in res:
        print(f"  -> Could not query OSSA mappings view: {err}", flush=True)
        print(f"  -> Falling back to bulk mapping attempt...", flush=True)
        return apply_field_mappings_bulk()

    response = res["response"]
    already_mapped = response.get("properties", {})
    unmapped_rules = response.get("unmapped_field_aliases", [])
    unmapped_index = response.get("unmapped_index_fields", [])

    print(f"  -> Already mapped: {len(already_mapped)}", flush=True)
    print(f"  -> Unmapped rule fields: {len(unmapped_rules)}", flush=True)
    print(f"  -> Unmapped index fields: {len(unmapped_index)}", flush=True)

    if not unmapped_rules:
        print(f"  -> All rule fields are already mapped!", flush=True)
        return len(already_mapped)

    # 2. Get concrete index fields to validate paths
    index_fields = get_index_fields()
    print(f"  -> Index has {len(index_fields)} concrete fields.", flush=True)

    # 3. Build mappings only for fields OSSA says are unmapped AND
    #    where we have a known mapping AND the target exists in the index
    to_map = {}
    skipped_no_mapping = []
    skipped_no_field = []

    for rule_field in unmapped_rules:
        if rule_field in FIELD_MAPPINGS:
            raw_field = FIELD_MAPPINGS[rule_field]
            if raw_field in index_fields:
                to_map[rule_field] = raw_field
            else:
                skipped_no_field.append(f"{rule_field} -> {raw_field}")
        else:
            skipped_no_mapping.append(rule_field)

    if skipped_no_mapping:
        print(f"  -> {len(skipped_no_mapping)} rule fields have no mapping in FIELD_MAPPINGS (ignored).", flush=True)
    if skipped_no_field:
        print(f"  -> {len(skipped_no_field)} mappings skipped (index field not found).", flush=True)
        for s in skipped_no_field[:5]:
            print(f"     {s}", flush=True)

    if not to_map:
        print(f"  -> No new mappings to apply.", flush=True)
        return len(already_mapped)

    print(f"  -> Applying {len(to_map)} field mappings...", flush=True)

    # 4. Apply mappings via OSSA API
    mappings_body = {
        "index_name": INDEX_NAME,
        "rule_topic": "windows",
        "partial": True,
        "alias_mappings": {
            "properties": {
                rule_field: {"type": "alias", "path": raw_field}
                for rule_field, raw_field in to_map.items()
            }
        }
    }

    map_res, map_err = req("POST", "/_plugins/_security_analytics/mappings",
                           mappings_body, timeout=60)
    if map_res:
        print(f"  -> Field mappings applied successfully ({len(to_map)} fields).", flush=True)
        return len(already_mapped) + len(to_map)
    else:
        print(f"  -> Bulk mapping failed: {map_err}", flush=True)
        print(f"  -> Trying one-by-one to isolate bad mappings...", flush=True)
        return apply_field_mappings_incremental(to_map, len(already_mapped))


def apply_field_mappings_incremental(to_map, already_count):
    """Apply mappings one at a time to identify which specific fields fail."""
    success = 0
    failures = []
    for rule_field, raw_field in to_map.items():
        body = {
            "index_name": INDEX_NAME,
            "rule_topic": "windows",
            "partial": True,
            "alias_mappings": {
                "properties": {
                    rule_field: {"type": "alias", "path": raw_field}
                }
            }
        }
        r, e = req("POST", "/_plugins/_security_analytics/mappings", body, timeout=15)
        if r:
            success += 1
        else:
            failures.append(f"{rule_field} -> {raw_field}: {e}")

    print(f"  -> Incremental: {success}/{len(to_map)} succeeded, {len(failures)} failed.", flush=True)
    if failures:
        for f in failures[:10]:
            print(f"     FAIL: {f}", flush=True)
    return already_count + success


def apply_field_mappings_bulk():
    """Fallback: apply all FIELD_MAPPINGS without querying unmapped list first."""
    index_fields = get_index_fields()
    to_map = {}
    for rule_field, raw_field in FIELD_MAPPINGS.items():
        if raw_field in index_fields:
            to_map[rule_field] = raw_field

    if not to_map:
        print(f"  -> No index fields found to map.", flush=True)
        return 0

    print(f"  -> Bulk fallback: applying {len(to_map)} mappings...", flush=True)
    mappings_body = {
        "index_name": INDEX_NAME,
        "rule_topic": "windows",
        "partial": True,
        "alias_mappings": {
            "properties": {
                rule_field: {"type": "alias", "path": raw_field}
                for rule_field, raw_field in to_map.items()
            }
        }
    }
    r, e = req("POST", "/_plugins/_security_analytics/mappings", mappings_body, timeout=60)
    if r:
        print(f"  -> Bulk mapping applied ({len(to_map)} fields).", flush=True)
        return len(to_map)
    else:
        print(f"  -> Bulk failed: {e}", flush=True)
        print(f"  -> Trying incremental...", flush=True)
        return apply_field_mappings_incremental(to_map, 0)


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

    # Apply field mappings BEFORE detector creation
    print("[3/3] Applying field mappings...", flush=True)
    mapped_count = apply_field_mappings()
    print(f"[3/3] Total mapped fields: {mapped_count}", flush=True)
    time.sleep(2)

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

