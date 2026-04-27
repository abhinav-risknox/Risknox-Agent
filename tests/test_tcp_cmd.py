import socket
import json

HOST = '127.0.0.1'
PORT = 1515

payload = {
    "agent_id": "test-license-agent",
    "policy_type": "software_blocking",
    "policy_data": {
        "action": "block",
        "name": "Notepad",
        "executable": "notepad.exe"
    }
}

try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.sendall(json.dumps(payload).encode('utf-8'))
    print("TCP command sent successfully!")
except Exception as e:
    print(f"Error: {e}")
