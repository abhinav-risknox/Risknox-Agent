import urllib.request
import json

data = {
  "config": {
    "name": "windows-alerts-destination",
    "description": "test",
    "config_type": "webhook",
    "is_enabled": True,
    "webhook": {
      "url": "http://opensearch:9200/windows-alerts/_doc"
    }
  }
}
req = urllib.request.Request("http://localhost:9200/_plugins/_notifications/configs", data=json.dumps(data).encode('utf-8'), headers={'Content-Type': 'application/json'}, method='POST')
try:
    with urllib.request.urlopen(req) as response:
        print(response.read().decode('utf-8'))
except Exception as e:
    print(e)
