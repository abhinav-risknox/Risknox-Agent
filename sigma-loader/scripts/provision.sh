#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# RiskNox OpenSearch Security Analytics — Provisioner
#
# Replaces the old sigma-loader approach (sigma convert + custom monitors)
# with OSSA pre-packaged Sigma rules for reliable detection.
#
# Steps:
#   1. Wait for OpenSearch
#   2. Apply the rp-events index template (comprehensive keyword mapping)
#   3. Create the rp-events index
#   4. Discover pre-packaged Windows Sigma rule IDs
#   5. Create the "Windows-Threat-Detector" with those rule IDs
#   6. Create suffixed field aliases for the detector's compiled queries
#   7. Maintenance loop: re-apply aliases when new fields appear
# ─────────────────────────────────────────────────────────────────────────────

OPENSEARCH_URL="${OPENSEARCH_URL:-http://opensearch:9200}"
INDEX_NAME="${INDEX_NAME:-rp-events}"
DETECTOR_NAME="${DETECTOR_NAME:-Windows-Threat-Detector}"

echo "=== RiskNox Provisioner starting (target=$OPENSEARCH_URL, index=$INDEX_NAME) ==="

# ─────────────────────────────────────────────
# 1. Wait for OpenSearch
# ─────────────────────────────────────────────
echo "=== [1/6] Waiting for OpenSearch..."
until curl -sf "$OPENSEARCH_URL/_cluster/health" | grep -q '"status":"green"\|"status":"yellow"'; do
  sleep 5
done
echo "OpenSearch is ready."

# ─────────────────────────────────────────────
# 2. Apply index template
# ─────────────────────────────────────────────
echo "=== [2/6] Applying index template (matches '${INDEX_NAME}*')..."

# Use the comprehensive template.json (covers all winlog.event_data fields)
TEMPLATE_FILE="/etc/sigma/template.json"
if [ ! -f "$TEMPLATE_FILE" ]; then
  echo "ERROR: $TEMPLATE_FILE not found"
  exit 1
fi

RESP=$(curl -s -o /tmp/tmpl.json -w "%{http_code}" \
  -X PUT "$OPENSEARCH_URL/_index_template/windows_events_template" \
  -H 'Content-Type: application/json' \
  -d @"$TEMPLATE_FILE")
if [ "${RESP:0:1}" = "2" ]; then
  echo "Index template applied (HTTP $RESP)."
else
  echo "WARNING: template apply HTTP $RESP: $(cat /tmp/tmpl.json)"
fi

# Increase limits for large rule sets
echo "Configuring OpenSearch cluster settings..."
curl -s -X PUT "$OPENSEARCH_URL/_cluster/settings" \
  -H "Content-Type: application/json" \
  -d '{
    "persistent": {
      "plugins.alerting.monitor.max_monitors": 10000,
      "script.max_compilations_rate": "10000/1m",
      "indices.query.bool.max_clause_count": 10000
    }
  }'

# ─────────────────────────────────────────────
# 3 - 7: Python does the heavy lifting
# ─────────────────────────────────────────────
python3 /etc/sigma/provision.py "$OPENSEARCH_URL" "$INDEX_NAME" "$DETECTOR_NAME"
