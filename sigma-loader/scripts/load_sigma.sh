#!/bin/bash

OPENSEARCH_URL="http://opensearch:9200"
MONITORS_DIR="/etc/sigma/converted"
RULES_DIR="/etc/sigma/rules/sigma/rules/windows"

echo "Waiting for OpenSearch to be ready..."
until curl -s "$OPENSEARCH_URL/_cluster/health" | grep -q '"status":"green"\|"status":"yellow"'; do
  sleep 5
done

# We will configure an index template first for windows-events
echo "Configuring OpenSearch index template..."
curl -s -X PUT "$OPENSEARCH_URL/_index_template/windows_events_template" \
  -H "Content-Type: application/json" \
  -d '{
    "index_patterns": ["windows-events*"],
    "template": {
      "mappings": {
        "properties": {
          "timestamp": { "type": "date" },
          "event": { "properties": { "id": { "type": "long" } } }
        }
      }
    }
  }'
echo "Index template configured."

echo "Converting Sigma rules for Windows..."
mkdir -p "$MONITORS_DIR"

TOTAL_DIRS=$(find "$RULES_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l)
CURRENT=0

for dir in "$RULES_DIR"/*/; do
  if [ ! -d "$dir" ]; then continue; fi
  dir_name=$(basename "$dir")
  CURRENT=$((CURRENT+1))
  echo "[$CURRENT/$TOTAL_DIRS] Parsing Sigma rules in $dir_name..."
  
  # Run sigma convert on the subdirectory
  sigma convert -s -t opensearch_lucene -p /etc/sigma/pipeline.yml -f monitor_rule "$dir" -o "$MONITORS_DIR/${dir_name}.json" || echo "Warning: Sigma convert encountered issues in $dir_name but continuing."
done

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

echo "Loading Sigma monitors into OpenSearch..."

echo "Creating Alert Destination..."
DEST_ID=$(curl -s -X POST "$OPENSEARCH_URL/_plugins/_alerting/destinations" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "windows-alerts-destination",
    "type": "custom_webhook",
    "custom_webhook": {
      "path": "/windows-alerts/_doc",
      "host": "opensearch",
      "port": 9200,
      "scheme": "HTTP",
      "method": "POST"
    }
  }' | grep -o '"_id":"[^"]*' | cut -d'"' -f4)

if [ -z "$DEST_ID" ]; then
  # If creation failed because it already exists, fetch the existing ID
  DEST_ID=$(curl -s -X GET "$OPENSEARCH_URL/_plugins/_alerting/destinations" | grep -o '"id":"[^"]*"[^}]*"name":"windows-alerts-destination"' | grep -o '"id":"[^"]*' | cut -d'"' -f4 | head -n 1)
fi
echo "Destination ID: $DEST_ID"

cat << 'EOF' > /tmp/load_monitors.py
import sys, json, urllib.request, urllib.error

file_path = sys.argv[1]
url = sys.argv[2] + "/_plugins/_alerting/monitors"
dest_id = sys.argv[3]
headers = {'Content-Type': 'application/json'}

try:
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
except Exception as e:
    print(f"Error reading {file_path}: {e}")
    sys.exit(0)

data = []
decoder = json.JSONDecoder()
idx = 0
content = content.lstrip()
while idx < len(content):
    try:
        obj, end_idx = decoder.raw_decode(content[idx:])
        data.append(obj)
        idx += end_idx
        idx += len(content[idx:]) - len(content[idx:].lstrip())
    except Exception as e:
        print(f"JSON parsing error at index {idx} in {file_path}: {e}")
        break

if not isinstance(data, list):
    data = [data]

success = 0
for monitor in data:
    if "inputs" in monitor:
        for input in monitor["inputs"]:
            if "search" in input and "indices" in input["search"]:
                input["search"]["indices"] = ["windows-events*"]

    if dest_id and "triggers" in monitor:
        for trigger in monitor["triggers"]:
            t_data = trigger.get("query_level_trigger") or trigger.get("bucket_level_trigger")
            if t_data is not None:
                t_data["actions"] = [{
                    "name": "Write to windows-alerts",
                    "destination_id": dest_id,
                    "message_template": {
                        "source": "{\"rule_name\": \"{{ctx.monitor.name}}\", \"timestamp\": \"{{ctx.periodStart}}\"}",
                        "lang": "mustache"
                    },
                    "throttle_enabled": False,
                    "subject_template": {
                        "source": "Alert",
                        "lang": "mustache"
                    }
                }]

    req = urllib.request.Request(url, data=json.dumps(monitor).encode('utf-8'), headers=headers, method='POST')
    try:
        urllib.request.urlopen(req)
        success += 1
    except urllib.error.HTTPError as e:
        print(f"Failed to load a monitor: HTTP {e.code} - {e.read().decode('utf-8')}")
    except Exception as e:
        print(f"Failed to load a monitor: {e}")

print(f"Loaded {success}/{len(data)} monitors from {file_path.split('/')[-1]}")
EOF
# Add a sleep to ensure OpenSearch has fully settled (sometimes needed right after startup)
sleep 2

for monitor_file in "$MONITORS_DIR"/*.json; do
  [ -e "$monitor_file" ] || continue
  
  rule_name=$(basename "$monitor_file" .json)
  
  if [ -f "$monitor_file" ]; then
    python3 /tmp/load_monitors.py "$monitor_file" "$OPENSEARCH_URL" "$DEST_ID"
  fi
done

echo "Sigma rules loaded."

# Keep container running
echo "Manager initialized. Keeping container alive..."
tail -f /dev/null
