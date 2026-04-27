

import subprocess
import struct
import json
import time
import sys
import os

BUILD_DIR = r"C:\Users\User\Desktop\Agent\build"
EXE       = os.path.join(BUILD_DIR, "rp-antivirus.exe")
PIPE_NAME = r"\\.\pipe\rp-antivirus"
SCAN_PATH = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Public"

# ── Wire format helpers ───────────────────────────────────────────────────────
def send_json(pipe, obj):
    body = json.dumps(obj).encode("utf-8")
    header = struct.pack("<I", len(body))   # 4-byte LE length prefix
    pipe.write(header + body)
    pipe.flush()

def recv_json(pipe):
    header = pipe.read(4)
    if len(header) < 4:
        return None
    length = struct.unpack("<I", header)[0]
    body = pipe.read(length)
    return json.loads(body)

# ── Main ──────────────────────────────────────────────────────────────────────
if not os.path.exists(EXE):
    print(f"[ERROR] rp-antivirus.exe not found at: {EXE}")
    print("        Build the project first: cmake --build build -- -j4")
    sys.exit(1)

print(f"[*] Spawning rp-antivirus.exe ...")
proc = subprocess.Popen([EXE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Give the process time to create the Named Pipe
time.sleep(1.0)

print(f"[*] Connecting to pipe: {PIPE_NAME}")
try:
    pipe = open(PIPE_NAME, "r+b", buffering=0)
except OSError as e:
    print(f"[ERROR] Could not connect to pipe: {e}")
    proc.terminate()
    sys.exit(1)

# Send quick_scan command
cmd = {"action": "quick_scan", "path": SCAN_PATH}
print(f"[*] Sending command: {json.dumps(cmd)}")
send_json(pipe, cmd)

# Read events until "complete"
print(f"\n{'-'*60}")
threats = []
files_scanned = 0
start = time.time()

while True:
    event = recv_json(pipe)
    if event is None:
        print("[!] Pipe closed unexpectedly")
        break

    t = event.get("type", "unknown")

    if t == "progress":
        files_scanned = event.get("filesScanned", files_scanned)
        print(f"  [progress] Files scanned: {files_scanned}", end="\r")

    elif t == "threat":
        threats.append(event)
        print(f"\n  [THREAT] {event.get('file')} -> {event.get('threat')}")

    elif t == "error":
        print(f"\n  [ERROR] {event.get('message')}")
        break

    elif t == "complete":
        elapsed = time.time() - start
        print(f"\n{'-'*60}")
        print(f"[OK] Scan complete in {elapsed:.1f}s")
        print(f"    Files scanned : {event.get('filesScanned', files_scanned)}")
        print(f"    Threats found : {event.get('threats', len(threats))}")
        if threats:
            print("\n  Threats detected:")
            for th in threats:
                print(f"    * {th['file']} - {th['threat']}")
        break

pipe.close()
proc.wait(timeout=5)
print("\n[*] rp-antivirus.exe exited cleanly")

