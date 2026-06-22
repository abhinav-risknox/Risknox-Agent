import urllib.request
import json

req = urllib.request.Request("http://localhost:9200/_plugins/_alerting/monitors/_search", data=json.dumps({"query":{"match_all":{}}, "size":0, "track_total_hits": True}).encode('utf-8'), headers={'Content-Type': 'application/json'}, method='GET')
try:
    with urllib.request.urlopen(req) as response:
        print(response.read().decode('utf-8'))
except Exception as e:
    print(e)
