import urllib.request
import json

data = {
    "event": {"id": 4688, "channel": "Security"},
    "process": {"cmdline": "whoami.exe /priv", "path": "C:\\Windows\\System32\\whoami.exe"},
    "user": {"name": "Hacker"}
}

req = urllib.request.Request("http://localhost:9200/windows-events/_doc", data=json.dumps(data).encode('utf-8'), headers={'Content-Type': 'application/json'}, method='POST')
try:
    urllib.request.urlopen(req)
    print("Injected successfully.")
except Exception as e:
    print(e)
