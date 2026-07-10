#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# RiskNox OpenSearch Security Analytics — Provisioner
#
# Idempotent, self-contained setup that runs once at stack startup.
#
# Strategy: Data Prepper writes flat fields matching OSSA raw_field names
# (CommandLine, EventID, SubjectUserName, etc.). OSSA auto-maps these to
# internal ecs paths during detector creation — no manual aliases needed.
#
# Steps:
#   1. Wait for OpenSearch
#   2. Apply the index template (dynamic keyword mapping for all fields)
#   3. Python provisioner: create index, discover rules, create detector
# ─────────────────────────────────────────────────────────────────────────────

OPENSEARCH_URL="${OPENSEARCH_URL:-http://opensearch:9200}"
INDEX_NAME="${INDEX_NAME:-rp-events}"
DETECTOR_NAME="${DETECTOR_NAME:-Windows-Threat-Detector}"

echo "=== RiskNox Provisioner starting (target=$OPENSEARCH_URL, index=$INDEX_NAME) ==="

# ─────────────────────────────────────────────
# 1. Wait for OpenSearch
# ─────────────────────────────────────────────
echo "=== [1/3] Waiting for OpenSearch..."
until curl -sf "$OPENSEARCH_URL/_cluster/health" | grep -q '"status":"green"\|"status":"yellow"'; do
  sleep 5
done
echo "OpenSearch is ready."

# ─────────────────────────────────────────────
# 2. Apply index template
# ─────────────────────────────────────────────
echo "=== [2/3] Applying index template (matches '${INDEX_NAME}*')..."
RESP=$(curl -s -o /tmp/tmpl.json -w "%{http_code}" \
  -X PUT "$OPENSEARCH_URL/_index_template/windows_events_template" \
  -H 'Content-Type: application/json' \
  -d @/etc/sigma/template.json)
if [ "${RESP:0:1}" = "2" ]; then
  echo "Index template applied (HTTP $RESP)."
else
  echo "WARNING: template apply HTTP $RESP: $(cat /tmp/tmpl.json)"
fi

# ─────────────────────────────────────────────
# 3. Python provisioner: index + rules + detector
# ─────────────────────────────────────────────
python3 /etc/sigma/provision.py "$OPENSEARCH_URL" "$INDEX_NAME" "$DETECTOR_NAME"
