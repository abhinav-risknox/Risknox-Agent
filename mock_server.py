"""
Mock Event Receiver Server for Resolute Pulse Agent
Listens on http://localhost:8443/events and logs received events
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
from datetime import datetime
import os
import traceback

# Log file path
LOG_FILE = "events.log"
JSON_LOG_FILE = "events.json"

def log_to_file(message):
    """Log message to file and console"""
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    log_line = f"[{timestamp}] {message}"
    print(log_line)
    with open(LOG_FILE, 'a', encoding='utf-8') as f:
        f.write(log_line + '\n')

def log_event_json(event_data):
    """Append event to JSON log file"""
    try:
        # Read existing events
        events = []
        if os.path.exists(JSON_LOG_FILE) and os.path.getsize(JSON_LOG_FILE) > 0:
            try:
                with open(JSON_LOG_FILE, 'r', encoding='utf-8') as f:
                    events = json.load(f)
            except json.JSONDecodeError:
                events = []
        
        # Add metadata and append events
        agent_id = event_data.get('agent_id', 'unknown')
        received_at = datetime.now().isoformat()
        
        for event in event_data.get('events', []):
            enriched_event = {
                'channel': event.get('channel'),
                'event_id': event.get('event_id'),
                'timestamp': event.get('timestamp'),
                'xml': event.get('xml'),
                'agent_id': agent_id,
                'received_at': received_at
            }
            events.append(enriched_event)
        
        # Write back
        with open(JSON_LOG_FILE, 'w', encoding='utf-8') as f:
            json.dump(events, f, indent=2, ensure_ascii=False)
        
        log_to_file(f"Saved {len(event_data.get('events', []))} events to {JSON_LOG_FILE}")
            
    except Exception as e:
        log_to_file(f"Error writing JSON log: {e}")
        traceback.print_exc()

class EventHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path == '/events':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length).decode('utf-8')
            
            agent_id = self.headers.get('X-Agent-ID', 'Unknown')
            
            log_to_file("=" * 60)
            log_to_file(f"Event Batch Received from Agent: {agent_id}")
            log_to_file("=" * 60)
            
            event_count = 0
            try:
                data = json.loads(body)
                events = data.get('events', [])
                event_count = len(events)
                log_to_file(f"Events in batch: {event_count}")
                
                # Log to JSON file
                log_event_json(data)
                
                for i, event in enumerate(events):
                    log_to_file(f"  Event {i+1}:")
                    log_to_file(f"    Channel: {event.get('channel', 'Unknown')}")
                    log_to_file(f"    Event ID: {event.get('event_id', 'Unknown')}")
                    log_to_file(f"    Timestamp: {event.get('timestamp', 'Unknown')}")
                    
                    # Show XML preview
                    xml = event.get('xml', '')
                    if xml:
                        preview = xml[:200].replace('\n', ' ').replace('\r', '')
                        log_to_file(f"    XML: {preview}...")
                            
            except json.JSONDecodeError as e:
                log_to_file(f"Invalid JSON: {e}")
            
            # Send success response
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = json.dumps({"status": "ok", "received": event_count})
            self.wfile.write(response.encode())
            
        else:
            self.send_response(404)
            self.end_headers()
    
    def log_message(self, format, *args):
        pass

def run_server(port=8443):
    server_address = ('', port)
    httpd = HTTPServer(server_address, EventHandler)
    
    print(f"""
╔════════════════════════════════════════════════════════════╗
║     Resolute Pulse - Mock Event Receiver Server            ║
╠════════════════════════════════════════════════════════════╣
║  Listening on: http://localhost:{port}/events                ║
║  Log file:     {LOG_FILE:<43} ║
║  JSON log:     {JSON_LOG_FILE:<43} ║
║  Press Ctrl+C to stop                                      ║
╚════════════════════════════════════════════════════════════╝
""")
    log_to_file("Server started")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log_to_file("Server stopped")
        print("\nServer stopped.")
        httpd.shutdown()

if __name__ == '__main__':
    run_server()
