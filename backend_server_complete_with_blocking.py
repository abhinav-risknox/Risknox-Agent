"""
RiskNoX Security Agent - Backend API Server
Provides secure endpoints for antivirus, web blocking, patch management, and software blocking
"""
import os
import sys

# Set encoding hint for Python (doesn't modify streams)
if 'PYTHONIOENCODING' not in os.environ:
    os.environ['PYTHONIOENCODING'] = 'utf-8:replace'

import json
import subprocess
import threading
import time
import hashlib
import secrets
import schedule
from datetime import datetime, timedelta
from pathlib import Path
import win32com.client
import pythoncom
import winreg
import psutil

from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

# Initialize Flask app
flask_app = Flask(__name__)
CORS(flask_app)

# Configuration
if getattr(sys, 'frozen', False):
    BASE_DIR = Path(sys.executable).parent
    print(f"Running as compiled executable from: {BASE_DIR}")
else:
    BASE_DIR = Path(__file__).parent
    print(f"Running as Python script from: {BASE_DIR}")

CONFIG_DIR = BASE_DIR / "config"
VENDOR_DIR = BASE_DIR / "vendor"
LOGS_DIR = BASE_DIR / "logs"
WEB_DIR = BASE_DIR / "web"

# Create directories if they don't exist
LOGS_DIR.mkdir(exist_ok=True)
CONFIG_DIR.mkdir(exist_ok=True)

# Security configuration
ADMIN_TOKENS = {}  # In production, use database
SCAN_SESSIONS = {}  # Active scan sessions
SCHEDULED_SCANS = {}  # Scheduled scan configurations

# Global state for process monitors
ACTIVE_MONITORS = {}
MONITOR_LOCK = threading.Lock()

# Debug settings
DEBUG_MODE = True

# Software Blocking Log File
SOFTWARE_BLOCKING_LOG = LOGS_DIR / "software_blocking.log"

# ============================================================================
# LOGGING FUNCTIONS
# ============================================================================

def safe_print(message, level="INFO"):
    """Safely print messages, handling encoding issues"""
    try:
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_msg = f"[{timestamp}] [{level}] {message}"
        print(log_msg)
    except UnicodeEncodeError:
        try:
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            safe_msg = str(message).encode('ascii', errors='replace').decode('ascii')
            print(f"[{timestamp}] [{level}] {safe_msg}")
        except:
            try:
                print(str(message).encode('ascii', errors='ignore').decode('ascii'))
            except:
                pass

def safe_str(value):
    """Convert any value to ASCII-safe string"""
    try:
        return str(value).encode('ascii', errors='replace').decode('ascii')
    except:
        return str(value)

def debug_log(message, level="INFO"):
    """Print debug messages with timestamp"""
    if DEBUG_MODE:
        safe_print(message, level)

def log_software_blocking(message, level="INFO"):
    """Log software blocking operations to dedicated log file"""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    log_line = f"[{timestamp}] [{level}] {message}"
    
    # Console output
    try:
        print(log_line)
    except:
        pass
    
    # File output
    try:
        with open(SOFTWARE_BLOCKING_LOG, 'a', encoding='utf-8') as f:
            f.write(log_line + "\n")
    except:
        pass


class SecurityAgent:
        
    def __init__(self):
        self.clamav_path = VENDOR_DIR / "clamscan.exe"
        self.clamd_path = VENDOR_DIR / "clamd.exe"
        self.freshclam_path = VENDOR_DIR / "freshclam.exe"
        self.hosts_file = Path("C:/Windows/System32/drivers/etc/hosts")
        self.blocked_urls_file = CONFIG_DIR / "blocked_urls.json"
        self.blocked_apps_file = CONFIG_DIR / "blocked_apps.json"
        
        # Application discovery cache
        self._app_cache = None
        self._app_cache_time = None
        self._app_cache_duration = 300  # 5 minutes cache
    
    # ========================================================================
    # ANTIVIRUS FUNCTIONS (PRESERVED)
    # ========================================================================
    
    def _check_clamav_databases(self):
        """Check if ClamAV databases are available"""
        db_path = VENDOR_DIR / "database"
        main_cvd = db_path / "main.cvd"
        main_cld = db_path / "main.cld"
        daily_cvd = db_path / "daily.cvd"
        daily_cld = db_path / "daily.cld"
        
        has_main = (main_cvd.exists() and main_cvd.stat().st_size > 1000) or \
                   (main_cld.exists() and main_cld.stat().st_size > 1000)
        has_daily = (daily_cvd.exists() and daily_cvd.stat().st_size > 1000) or \
                    (daily_cld.exists() and daily_cld.stat().st_size > 1000)
        
        return has_main and has_daily
    
    def _add_scan_log(self, session_id, message):
        """Add a log message to the scan session"""
        if session_id in SCAN_SESSIONS:
            timestamp = datetime.now().strftime("%H:%M:%S")
            log_entry = f"[{timestamp}] {message}"
            SCAN_SESSIONS[session_id]['scan_log'].append(log_entry)
            
            if len(SCAN_SESSIONS[session_id]['scan_log']) > 100:
                SCAN_SESSIONS[session_id]['scan_log'] = SCAN_SESSIONS[session_id]['scan_log'][-100:]
    
    def _update_virus_database(self):
        """Update ClamAV virus database using freshclam"""
        try:
            db_dir = VENDOR_DIR / "database"
            db_dir.mkdir(exist_ok=True)
            
            print("Updating ClamAV virus database...")
            
            log_file = LOGS_DIR / "database_updates.log"
            
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"\n{'='*50}\n")
                f.write(f"Database Update Attempt\n")
                f.write(f"Time: {datetime.now().isoformat()}\n")
                f.write(f"{'='*50}\n")
            
            result = subprocess.run([
                str(self.freshclam_path),
                f'--datadir={db_dir}',
                '--quiet',
                '--no-warnings'
            ], capture_output=True, text=True, timeout=300)
            
            with open(log_file, 'a', encoding='utf-8') as f:
                if result.returncode == 0:
                    f.write("Status: SUCCESS\n")
                    print("Virus database updated successfully")
                else:
                    f.write(f"Status: WARNING (return code: {result.returncode})\n")
                    f.write(f"Error output: {result.stderr}\n")
                    print(f"Database update warning: {result.stderr}")
                
                if result.stdout:
                    f.write(f"Output: {result.stdout}\n")
            
            if result.returncode == 0:
                return True
            else:
                return (db_dir / "main.cvd").exists() or (db_dir / "main.cld").exists()
                
        except subprocess.TimeoutExpired:
            print("Database update timed out")
            return False
        except Exception as e:
            print(f"Error updating database: {e}")
            return False
    
    def setup_database_updates(self):
        """Schedule daily virus database updates"""
        def update_job():
            print("Running scheduled virus database update...")
            self._update_virus_database()
        
        schedule.every().day.at("02:00").do(update_job)
        print("Scheduled daily virus database updates at 02:00")
    
    def generate_admin_token(self, username, password):
        """Generate admin authentication token"""
        if username == "admin" and password == "RiskNoX@2024":
            token = secrets.token_hex(32)
            ADMIN_TOKENS[token] = {
                'username': username,
                'created_at': datetime.now(),
                'expires_at': datetime.now() + timedelta(hours=8)
            }
            return token
        return None
    
    def verify_admin_token(self, token):
        """Verify admin token"""
        if token in ADMIN_TOKENS:
            if datetime.now() < ADMIN_TOKENS[token]['expires_at']:
                return True
            else:
                del ADMIN_TOKENS[token]
        return False
    
    def scan_directory(self, scan_path, session_id, is_scheduled=False):
        """Perform ACTUAL antivirus scan using ClamAV"""
        try:
            log_file = LOGS_DIR / f"scan_{session_id}.log"
            
            if not Path(scan_path).exists():
                raise Exception(f"Scan path does not exist: {scan_path}")
            
            SCAN_SESSIONS[session_id] = {
                'status': 'initializing',
                'path': scan_path,
                'started_at': datetime.now(),
                'log_file': str(log_file),
                'files_scanned': 0,
                'threats_found': 0,
                'total_files': 0,
                'is_scheduled': is_scheduled,
                'progress_percent': 0,
                'scan_log': [],
                'threats': [],
                'errors': [],
                'last_update': datetime.now() 
            }
            
            self._add_scan_log(session_id, "Initializing ClamAV scan engine...")
            
            if not self.clamav_path.exists():
                raise Exception("ClamAV executable not found. Please install ClamAV.")
            
            db_dir = VENDOR_DIR / "database"
            if not self._check_clamav_databases():
                self._add_scan_log(session_id, "Virus database not found. Updating...")
                if not self._update_virus_database():
                    raise Exception("Failed to download virus database")
            
            self._add_scan_log(session_id, f"Starting scan of: {scan_path}")
            SCAN_SESSIONS[session_id]['status'] = 'scanning'
            
            clamscan_cmd = [
                str(self.clamav_path),
                '--recursive',
                '--infected',
                '--bell',
                f'--database={db_dir}',
                f'--log={log_file}',
                '--verbose',
                scan_path
            ]
            
            process = subprocess.Popen(
                clamscan_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1
            )
            
            threats = []
            files_scanned = 0
            last_progress_update = time.time()
            
            for line in process.stdout:
                line = line.strip()
    
                if session_id not in SCAN_SESSIONS:
                    process.terminate()
                    return
    
                if line.startswith('Scanning '):
                    files_scanned += 1
                    SCAN_SESSIONS[session_id]['files_scanned'] = files_scanned
                    SCAN_SESSIONS[session_id]['last_update'] = datetime.now()
        
                    current_time = time.time()
                    if current_time - last_progress_update >= 2:
                        if files_scanned < 100:
                            progress = min(files_scanned, 50)
                        elif files_scanned < 500:
                            progress = 50 + (files_scanned - 100) // 10
                        else:
                            progress = min(70 + (files_scanned - 500) // 50, 90)
            
                        SCAN_SESSIONS[session_id]['progress_percent'] = progress
                        last_progress_update = current_time
        
                    if files_scanned % 10 == 0:
                        self._add_scan_log(session_id, f"Scanned {files_scanned} files...")
            
                elif 'FOUND' in line:
                    if ': ' in line and 'FOUND' in line:
                        last_colon_index = line.rfind(': ')
                        file_path = line[:last_colon_index].strip()
                        threat_part = line[last_colon_index + 2:].strip()
                        threat_name = threat_part.replace('FOUND', '').strip()

                        threats.append({
                            'file': file_path,
                            'threat': threat_name,
                            'timestamp': datetime.now().isoformat()
                        })

                        self._add_scan_log(session_id, f"THREAT DETECTED: {threat_name}")
                        self._add_scan_log(session_id, f"  Location: {file_path}")

                        SCAN_SESSIONS[session_id]['threats_found'] = len(threats)

            return_code = process.wait()
            stderr_output = process.stderr.read()
            
            SCAN_SESSIONS[session_id].update({
                'status': 'completed',
                'completed_at': datetime.now(),
                'files_scanned': files_scanned,
                'threats_found': len(threats),
                'threats': threats,
                'return_code': return_code,
                'progress_percent': 100
            })
            
            duration = (datetime.now() - SCAN_SESSIONS[session_id]['started_at']).total_seconds()
            self._add_scan_log(session_id, f"Scan completed in {duration:.1f} seconds")
            self._add_scan_log(session_id, f"Files scanned: {files_scanned}")
            self._add_scan_log(session_id, f"Threats found: {len(threats)}")
            
            if len(threats) == 0:
                self._add_scan_log(session_id, "System is clean - no threats detected")
            
            return True
            
        except Exception as e:
            error_msg = f'Scan error: {str(e)}'
            self._add_scan_log(session_id, f"ERROR: {error_msg}")
            SCAN_SESSIONS[session_id].update({
                'status': 'error',
                'error': error_msg,
                'progress_percent': 0
            })
            return False

    def scan_full_system(self, session_id):
        """Perform comprehensive full system scan"""
        SCAN_SESSIONS[session_id] = {
            'session_id': session_id,
            'status': 'initializing',
            'started_at': datetime.now(),
            'path': 'Full System Scan',
            'files_scanned': 0,
            'threats_found': 0,
            'progress_percent': 0,
            'scan_log': [],
            'threats': [],
            'last_update': datetime.now(),
            'total_files': 0,
            'current_file': '',
            'scan_speed': 0,
            'errors': []
        }
        
        self._add_scan_log(session_id, "Initializing full system scan...")
        
        try:
            drives = []
            self._add_scan_log(session_id, "Detecting system drives...")
            
            for partition in psutil.disk_partitions():
                try:
                    usage = psutil.disk_usage(partition.mountpoint)
                    if usage and usage.total > 0:
                        drives.append(partition.mountpoint)
                        self._add_scan_log(session_id, f"Available drive: {partition.mountpoint}")
                except:
                    continue
            
            critical_dirs = [
                ("C:\\Users", "User profiles"),
                ("C:\\Program Files", "Applications"),
                ("C:\\Program Files (x86)", "32-bit applications"),
                ("C:\\ProgramData", "Application data"),
                ("C:\\Windows\\System32", "System files"),
            ]
            
            scan_targets = []
            for drive in drives:
                if Path(drive).exists():
                    scan_targets.append((drive, "Full drive scan"))
            
            for dir_path, description in critical_dirs:
                if Path(dir_path).exists():
                    covered = any(dir_path.startswith(drive) for drive, _ in scan_targets)
                    if not covered:
                        scan_targets.append((dir_path, description))
            
            total_targets = len(scan_targets)
            SCAN_SESSIONS[session_id]['status'] = 'scanning'
            
            for i, (scan_path, description) in enumerate(scan_targets):
                if session_id not in SCAN_SESSIONS:
                    return
                
                overall_progress = int((i / total_targets) * 100)
                SCAN_SESSIONS[session_id]['progress_percent'] = overall_progress
                
                self._add_scan_log(session_id, f"[{i+1}/{total_targets}] Scanning: {scan_path}")
                
                temp_session = f"{session_id}_sys_{i}"
                
                try:
                    self.scan_directory(scan_path, temp_session, is_scheduled=False)
                    
                    if temp_session in SCAN_SESSIONS:
                        temp_results = SCAN_SESSIONS[temp_session]
                        
                        SCAN_SESSIONS[session_id]['files_scanned'] += temp_results.get('files_scanned', 0)
                        SCAN_SESSIONS[session_id]['threats_found'] += temp_results.get('threats_found', 0)
                        
                        if temp_results.get('threats'):
                            SCAN_SESSIONS[session_id]['threats'].extend(temp_results['threats'])
                        
                        del SCAN_SESSIONS[temp_session]
                except:
                    continue
            
            end_time = datetime.now()
            duration = end_time - SCAN_SESSIONS[session_id]['started_at']
            
            SCAN_SESSIONS[session_id].update({
                'progress_percent': 100,
                'status': 'completed',
                'completed_at': end_time,
                'scan_duration': duration.total_seconds()
            })
            
            self._add_scan_log(session_id, "Full system scan completed!")
            
        except Exception as e:
            SCAN_SESSIONS[session_id].update({
                'status': 'error',
                'error': str(e),
                'progress_percent': 0
            })

    def scan_quick_system(self, session_id):
        """Perform quick system scan"""
        SCAN_SESSIONS[session_id] = {
            'session_id': session_id,
            'status': 'initializing',
            'started_at': datetime.now(),
            'path': 'Quick System Scan',
            'files_scanned': 0,
            'threats_found': 0,
            'progress_percent': 0,
            'scan_log': [],
            'threats': [],
            'last_update': datetime.now(),
            'total_files': 0,
            'errors': []
        }
        
        self._add_scan_log(session_id, "Initializing quick system scan...")
        
        try:
            quick_targets = [
                (os.path.expanduser("~\\Downloads"), "Downloads"),
                (os.path.expanduser("~\\Desktop"), "Desktop"),
                (os.path.expanduser("~\\Documents"), "Documents"),
                ("C:\\Windows\\Temp", "Windows Temp"),
                ("C:\\Temp", "System Temp"),
            ]
            
            accessible_targets = []
            for path, description in quick_targets:
                if Path(path).exists():
                    accessible_targets.append((path, description))
            
            total_targets = len(accessible_targets)
            SCAN_SESSIONS[session_id]['status'] = 'scanning'
            
            for i, (scan_path, description) in enumerate(accessible_targets):
                if session_id not in SCAN_SESSIONS:
                    return
                
                progress = int((i / total_targets) * 90)
                SCAN_SESSIONS[session_id]['progress_percent'] = progress
                
                self._add_scan_log(session_id, f"[{i+1}/{total_targets}] {description}")
                
                temp_session = f"{session_id}_quick_{i}"
                
                try:
                    self.scan_directory(scan_path, temp_session, is_scheduled=False)
                    
                    if temp_session in SCAN_SESSIONS:
                        temp_results = SCAN_SESSIONS[temp_session]
                        
                        SCAN_SESSIONS[session_id]['files_scanned'] += temp_results.get('files_scanned', 0)
                        SCAN_SESSIONS[session_id]['threats_found'] += temp_results.get('threats_found', 0)
                        
                        if temp_results.get('threats'):
                            SCAN_SESSIONS[session_id]['threats'].extend(temp_results['threats'])
                        
                        del SCAN_SESSIONS[temp_session]
                except:
                    continue
            
            end_time = datetime.now()
            duration = end_time - SCAN_SESSIONS[session_id]['started_at']
            
            SCAN_SESSIONS[session_id].update({
                'progress_percent': 100,
                'status': 'completed',
                'completed_at': end_time,
                'scan_duration': duration.total_seconds()
            })
            
            self._add_scan_log(session_id, "Quick scan completed!")
            
        except Exception as e:
            SCAN_SESSIONS[session_id].update({
                'status': 'error',
                'error': str(e),
                'progress_percent': 0
            })
    
    def cancel_scan(self, session_id):
        """Cancel an active scan"""
        try:
            if session_id not in SCAN_SESSIONS:
                return {'success': False, 'message': 'Scan session not found'}
            
            session = SCAN_SESSIONS[session_id]
            
            if session['status'] not in ['initializing', 'scanning']:
                return {'success': False, 'message': f'Cannot cancel scan with status: {session["status"]}'}
            
            self._add_scan_log(session_id, "Scan cancellation requested")
            
            SCAN_SESSIONS[session_id].update({
                'status': 'cancelled',
                'cancelled_at': datetime.now()
            })
            
            return {'success': True, 'message': 'Scan cancelled successfully'}
            
        except Exception as e:
            return {'success': False, 'message': str(e)}
    
    def get_database_info(self):
        """Get virus database information"""
        try:
            db_dir = VENDOR_DIR / "database"
            
            if not db_dir.exists():
                return {'status': 'not_found', 'message': 'Database directory does not exist'}
            
            database_info = {
                'status': 'available',
                'databases': [],
                'total_size_mb': 0,
                'last_update': None
            }
            
            db_files = ['main.cvd', 'main.cld', 'daily.cvd', 'daily.cld']
            latest_time = None
            
            for db_file in db_files:
                file_path = db_dir / db_file
                if file_path.exists():
                    stat_info = file_path.stat()
                    size_mb = stat_info.st_size / (1024 * 1024)
                    modified_time = datetime.fromtimestamp(stat_info.st_mtime)
                    
                    database_info['databases'].append({
                        'name': db_file,
                        'size_mb': round(size_mb, 2),
                        'last_modified': modified_time.isoformat()
                    })
                    
                    database_info['total_size_mb'] += size_mb
                    
                    if latest_time is None or modified_time > latest_time:
                        latest_time = modified_time
            
            database_info['total_size_mb'] = round(database_info['total_size_mb'], 2)
            database_info['last_update'] = latest_time.isoformat() if latest_time else None
            
            return database_info
            
        except Exception as e:
            return {'status': 'error', 'message': str(e)}
    
    # ========================================================================
    # WEB BLOCKING FUNCTIONS (PRESERVED)
    # ========================================================================
    
    def block_url(self, url):
        """Block a URL"""
        try:
            blocked_urls = self.load_blocked_urls()
            clean_url = url.replace('http://', '').replace('https://', '').strip()
            
            if any(u.get('url') == clean_url for u in blocked_urls):
                return True
            
            blocked_urls.append({
                'url': clean_url,
                'blocked_at': datetime.now().isoformat(),
                'status': 'active',
                'method': 'hosts_file'
            })
            
            self.save_blocked_urls(blocked_urls)
            return self.update_hosts_file()
            
        except Exception as e:
            print(f"Error blocking URL: {e}")
            return False
    
    def unblock_url(self, url):
        """Unblock a URL"""
        try:
            blocked_urls = self.load_blocked_urls()
            clean_url = url.replace('http://', '').replace('https://', '').strip()
            
            blocked_urls = [u for u in blocked_urls if u.get('url') != clean_url]
            self.save_blocked_urls(blocked_urls)
            
            return self.update_hosts_file()
            
        except Exception as e:
            print(f"Error unblocking URL: {e}")
            return False
    
    def load_blocked_urls(self):
        """Load blocked URLs"""
        try:
            if self.blocked_urls_file.exists():
                with open(self.blocked_urls_file, 'r') as f:
                    return json.load(f)
        except:
            pass
        return []
    
    def save_blocked_urls(self, urls):
        """Save blocked URLs"""
        CONFIG_DIR.mkdir(exist_ok=True)
        with open(self.blocked_urls_file, 'w') as f:
            json.dump(urls, f, indent=2)
    
    def update_hosts_file(self):
        """Update hosts file"""
        try:
            blocked_urls = self.load_blocked_urls()
            
            with open(self.hosts_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            lines = content.split('\n')
            cleaned_lines = [line for line in lines if not line.strip().endswith('# RiskNoX Block')]
            
            if not any('# RiskNoX Security Agent' in line for line in cleaned_lines):
                cleaned_lines.append('')
                cleaned_lines.append('# RiskNoX Security Agent - Blocked URLs')
            
            for url_data in blocked_urls:
                if url_data.get('status') == 'active':
                    url = url_data['url']
                    cleaned_lines.append(f"127.0.0.1 {url} # RiskNoX Block")
                    cleaned_lines.append(f"127.0.0.1 www.{url} # RiskNoX Block")
            
            new_content = '\n'.join(cleaned_lines)
            
            with open(self.hosts_file, 'w', encoding='utf-8') as f:
                f.write(new_content)
            
            subprocess.run(["ipconfig", "/flushdns"], capture_output=True)
            return True
            
        except Exception as e:
            print(f"Error updating hosts file: {e}")
            return False
    
    # ========================================================================
    # PATCH MANAGEMENT FUNCTIONS (PRESERVED)
    # ========================================================================
    
    def get_patch_info(self):
        """Get Windows patch information"""
        try:
            pythoncom.CoInitialize()
            
            update_session = win32com.client.Dispatch("Microsoft.Update.Session")
            update_searcher = update_session.CreateUpdateSearcher()
            
            installed_result = update_searcher.Search("IsInstalled=1")
            installed_list = []
            
            for update in installed_result.Updates:
                is_driver = any(cat.Name == "Drivers" for cat in update.Categories)
                if is_driver:
                    continue
                
                kb_numbers = ", ".join([f"KB{kb}" for kb in update.KBArticleIDs]) if update.KBArticleIDs else "N/A"
                
                installed_list.append({
                    "title": update.Title,
                    "kb": kb_numbers,
                    "update_id": update.Identity.UpdateID
                })
            
            pending_result = update_searcher.Search("IsInstalled=0")
            pending_list = []
            
            for update in pending_result.Updates:
                is_driver = any(cat.Name == "Drivers" for cat in update.Categories)
                if is_driver:
                    continue
                
                kb_numbers = ", ".join([f"KB{kb}" for kb in update.KBArticleIDs]) if update.KBArticleIDs else "N/A"
                
                pending_list.append({
                    "title": update.Title,
                    "kb": kb_numbers,
                    "update_id": update.Identity.UpdateID
                })
            
            return {
                "status": "success",
                "updates": pending_list,
                "installed_updates": installed_list
            }
            
        except Exception as e:
            return {"status": "error", "message": str(e)}
    
    def install_updates(self, update_ids):
        """Install Windows updates"""
        try:
            pythoncom.CoInitialize()
            
            update_session = win32com.client.Dispatch("Microsoft.Update.Session")
            update_searcher = update_session.CreateUpdateSearcher()
            
            search_result = update_searcher.Search("IsInstalled=0")
            
            to_install = win32com.client.Dispatch("Microsoft.Update.UpdateColl")
            
            for update in search_result.Updates:
                if update.Identity.UpdateID in update_ids:
                    to_install.Add(update)
            
            if to_install.Count == 0:
                return {"status": "error", "message": "No matching updates found"}
            
            downloader = update_session.CreateUpdateDownloader()
            downloader.Updates = to_install
            download_result = downloader.Download()
            
            if download_result.ResultCode != 2:
                return {"status": "error", "message": "Download failed"}
            
            installer = update_session.CreateUpdateInstaller()
            installer.Updates = to_install
            install_result = installer.Install()
            
            return {
                "status": "success",
                "installed": to_install.Count,
                "reboot_required": install_result.RebootRequired
            }
            
        except Exception as e:
            return {"status": "error", "message": str(e)}
    
    # ========================================================================
    # SOFTWARE BLOCKING FUNCTIONS (NEW - PROVEN METHODS)
    # ========================================================================
    
    def get_blocked_applications(self):
        """Load blocked applications from JSON"""
        try:
            if self.blocked_apps_file.exists():
                with open(self.blocked_apps_file, 'r', encoding='utf-8') as f:
                    return json.load(f)
            return []
        except Exception as e:
            log_software_blocking(f"Error loading blocked apps: {e}", "ERROR")
            return []
    
    def save_blocked_applications(self, blocked_apps):
        """Save blocked applications to JSON"""
        try:
            CONFIG_DIR.mkdir(exist_ok=True)
            with open(self.blocked_apps_file, 'w', encoding='utf-8') as f:
                json.dump(blocked_apps, f, indent=2, ensure_ascii=True)
            return True
        except Exception as e:
            log_software_blocking(f"Error saving blocked apps: {e}", "ERROR")
            return False
    
    def terminate_process(self, executable):
        """Kill all running instances (proven method)"""
        log_software_blocking(f"Terminating: {executable}")
        killed = 0
        exe_lower = executable.lower()
        exe_no_ext = exe_lower.replace('.exe', '')
        
        # Method 1: psutil kill
        for proc in psutil.process_iter(['name', 'pid']):
            try:
                pname = proc.info['name'].lower()
                if pname == exe_lower or pname == exe_no_ext:
                    log_software_blocking(f"  Killing PID: {proc.info['pid']}")
                    proc.kill()
                    proc.wait(timeout=2)
                    killed += 1
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
            except Exception as e:
                log_software_blocking(f"  Error: {e}", "WARN")
        
        # Method 2: taskkill
        try:
            result = subprocess.run(
                ['taskkill', '/F', '/IM', executable, '/T'],
                capture_output=True,
                timeout=5
            )
            if result.returncode == 0:
                log_software_blocking(f"  Taskkill successful")
                killed += 1
        except:
            pass
        
        log_software_blocking(f"  Total killed: {killed}")
        return killed
    
    def block_with_registry(self, executable):
        """Apply triple registry blocking (proven method)"""
        log_software_blocking(f"Applying triple-registry block: {executable}")
        success_count = 0
        
        # METHOD 1: Group Policy
        try:
            log_software_blocking("  Method 1: Group Policy")
            base_path = r"SOFTWARE\Policies\Microsoft\Windows\Explorer"
            disallow_path = base_path + r"\DisallowRun"
            
            key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, base_path, 0, winreg.KEY_ALL_ACCESS)
            winreg.SetValueEx(key, "DisallowRun", 0, winreg.REG_DWORD, 1)
            winreg.CloseKey(key)
            
            key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, disallow_path, 0, winreg.KEY_ALL_ACCESS)
            
            idx = 1
            while True:
                try:
                    name, value, _ = winreg.EnumValue(key, idx - 1)
                    if value.lower() == executable.lower():
                        winreg.CloseKey(key)
                        success_count += 1
                        break
                    idx += 1
                except OSError:
                    winreg.SetValueEx(key, str(idx), 0, winreg.REG_SZ, executable)
                    winreg.CloseKey(key)
                    log_software_blocking(f"    ✓ Added at index {idx}")
                    success_count += 1
                    break
        except Exception as e:
            log_software_blocking(f"    Method 1 failed: {e}", "WARN")
        
        # METHOD 2: HKLM Standard
        try:
            log_software_blocking("  Method 2: HKLM Standard")
            base_path = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer"
            disallow_path = base_path + r"\DisallowRun"
            
            key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, base_path, 0, winreg.KEY_ALL_ACCESS)
            winreg.SetValueEx(key, "DisallowRun", 0, winreg.REG_DWORD, 1)
            winreg.CloseKey(key)
            
            key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, disallow_path, 0, winreg.KEY_ALL_ACCESS)
            
            idx = 1
            while True:
                try:
                    name, value, _ = winreg.EnumValue(key, idx - 1)
                    if value.lower() == executable.lower():
                        winreg.CloseKey(key)
                        success_count += 1
                        break
                    idx += 1
                except OSError:
                    winreg.SetValueEx(key, str(idx), 0, winreg.REG_SZ, executable)
                    winreg.CloseKey(key)
                    log_software_blocking(f"    ✓ Added at index {idx}")
                    success_count += 1
                    break
        except Exception as e:
            log_software_blocking(f"    Method 2 failed: {e}", "WARN")
        
        # METHOD 3: HKCU
        try:
            log_software_blocking("  Method 3: HKCU")
            base_path = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer"
            disallow_path = base_path + r"\DisallowRun"
            
            key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, base_path, 0, winreg.KEY_ALL_ACCESS)
            winreg.SetValueEx(key, "DisallowRun", 0, winreg.REG_DWORD, 1)
            winreg.CloseKey(key)
            
            key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, disallow_path, 0, winreg.KEY_ALL_ACCESS)
            
            idx = 1
            while True:
                try:
                    name, value, _ = winreg.EnumValue(key, idx - 1)
                    if value.lower() == executable.lower():
                        winreg.CloseKey(key)
                        success_count += 1
                        break
                    idx += 1
                except OSError:
                    winreg.SetValueEx(key, str(idx), 0, winreg.REG_SZ, executable)
                    winreg.CloseKey(key)
                    log_software_blocking(f"    ✓ Added at index {idx}")
                    success_count += 1
                    break
        except Exception as e:
            log_software_blocking(f"    Method 3 failed: {e}", "WARN")
        
        log_software_blocking(f"  Registry: {success_count}/3 methods succeeded")
        return success_count > 0
    
    def update_group_policy(self):
        """Force Group Policy update"""
        try:
            subprocess.run(["gpupdate", "/force"], capture_output=True, timeout=30)
            log_software_blocking("  Group Policy updated")
        except:
            pass
    
    def restart_explorer(self):
        """Restart Explorer (proven method)"""
        try:
            log_software_blocking("Restarting Explorer...")
            subprocess.run(
                ["taskkill", "/f", "/im", "explorer.exe"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            time.sleep(2)
            subprocess.Popen(["explorer.exe"])
            time.sleep(1)
            log_software_blocking("  ✓ Explorer restarted")
            return True
        except Exception as e:
            log_software_blocking(f"  Failed: {e}", "WARN")
            return False
    
    def start_process_monitor(self, executable):
        """Start aggressive process monitor"""
        exe_lower = executable.lower()
        exe_no_ext = exe_lower.replace('.exe', '')
        
        with MONITOR_LOCK:
            if exe_lower in ACTIVE_MONITORS:
                return
        
        def monitor_loop():
            log_software_blocking(f"Monitor started: {executable}")
            consecutive_kills = 0
            
            while True:
                blocked = self.get_blocked_applications()
                if not any(b.get('executable', '').lower() == exe_lower for b in blocked):
                    break
                
                killed_this_round = 0
                
                for proc in psutil.process_iter(['name', 'pid']):
                    try:
                        pname = proc.info['name'].lower()
                        if pname == exe_lower or pname == exe_no_ext:
                            log_software_blocking(f"  Monitor killing: {proc.info['name']} (PID: {proc.info['pid']})")
                            proc.kill()
                            proc.wait(timeout=1)
                            killed_this_round += 1
                            consecutive_kills += 1
                    except (psutil.NoSuchProcess, psutil.AccessDenied):
                        pass
                    except:
                        pass
                
                if consecutive_kills > 0 and consecutive_kills % 5 == 0:
                    try:
                        subprocess.run(
                            ['taskkill', '/F', '/IM', executable, '/T'],
                            capture_output=True,
                            timeout=3
                        )
                    except:
                        pass
                
                time.sleep(0.3)
            
            with MONITOR_LOCK:
                ACTIVE_MONITORS.pop(exe_lower, None)
            log_software_blocking(f"Monitor stopped: {executable} (kills: {consecutive_kills})")
        
        thread = threading.Thread(target=monitor_loop, daemon=True, name=f"Monitor-{executable}")
        
        with MONITOR_LOCK:
            ACTIVE_MONITORS[exe_lower] = thread
        
        thread.start()
    
    def stop_process_monitor(self, executable):
        """Stop process monitor"""
        with MONITOR_LOCK:
            ACTIVE_MONITORS.pop(executable.lower(), None)
    
    def remove_registry_block(self, executable):
        """Remove from all three registry locations"""
        log_software_blocking(f"Removing registry block: {executable}")
        removed = 0
        
        locations = [
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\Explorer\DisallowRun", "Group Policy"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer\DisallowRun", "HKLM"),
            (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer\DisallowRun", "HKCU"),
        ]
        
        for hive, path, name in locations:
            try:
                log_software_blocking(f"  Removing from {name}...")
                key = winreg.OpenKey(hive, path, 0, winreg.KEY_ALL_ACCESS)
                
                to_delete = []
                idx = 0
                while True:
                    try:
                        vname, value, _ = winreg.EnumValue(key, idx)
                        if value.lower() == executable.lower():
                            to_delete.append(vname)
                        idx += 1
                    except OSError:
                        break
                
                for vname in to_delete:
                    winreg.DeleteValue(key, vname)
                    removed += 1
                    log_software_blocking(f"    ✓ Removed [{vname}]")
                
                winreg.CloseKey(key)
            except FileNotFoundError:
                pass
            except Exception as e:
                log_software_blocking(f"    Error: {e}", "WARN")
        
        log_software_blocking(f"  Total removed: {removed}")
        return removed > 0
    
    def block_application(self, app_name, executable):
        """Block application (proven sequence)"""
        log_software_blocking("="*80)
        log_software_blocking(f"BLOCKING: {app_name} ({executable})")
        log_software_blocking("="*80)
        
        try:
            blocked_apps = self.get_blocked_applications()
            
            if any(b.get('executable', '').lower() == executable.lower() for b in blocked_apps):
                return {'success': True, 'message': 'Already blocked'}
            
            # Step 1: Kill processes
            killed = self.terminate_process(executable)
            
            # Step 2: Registry blocking
            registry_ok = self.block_with_registry(executable)
            
            # Step 3: Group Policy update
            self.update_group_policy()
            
            # Step 4: Restart Explorer
            explorer_restarted = self.restart_explorer()
            
            # Step 5: Start monitor
            self.start_process_monitor(executable)
            
            # Step 6: Kill again
            time.sleep(0.5)
            killed2 = self.terminate_process(executable)
            
            total_kills = killed + killed2
            
            # Save config
            blocked_apps.append({
                'name': app_name,
                'executable': executable,
                'blocked_at': datetime.now().isoformat(),
                'status': 'active',
                'kills': total_kills,
                'explorer_restarted': explorer_restarted,
                'registry_applied': registry_ok
            })
            self.save_blocked_applications(blocked_apps)
            
            log_software_blocking("="*80)
            log_software_blocking(f"BLOCKING COMPLETE: {executable}")
            log_software_blocking(f"  Kills: {total_kills}, Registry: {registry_ok}, Explorer: {explorer_restarted}")
            log_software_blocking("="*80)
            
            return {
                'success': True,
                'message': f'{app_name} blocked successfully',
                'details': {
                    'total_kills': total_kills,
                    'registry_applied': registry_ok,
                    'explorer_restarted': explorer_restarted,
                    'monitor_active': True
                }
            }
            
        except Exception as e:
            log_software_blocking(f"ERROR: {e}", "ERROR")
            return {'success': False, 'message': str(e)}
    
    def unblock_application(self, executable):
        """Unblock application (proven sequence)"""
        log_software_blocking("="*80)
        log_software_blocking(f"UNBLOCKING: {executable}")
        log_software_blocking("="*80)
        
        try:
            blocked_apps = self.get_blocked_applications()
            
            if not any(b.get('executable', '').lower() == executable.lower() for b in blocked_apps):
                return {'success': True, 'message': 'Not blocked'}
            
            # Step 1: Remove registry
            registry_removed = self.remove_registry_block(executable)
            
            # Step 2: Stop monitor
            self.stop_process_monitor(executable)
            
            # Step 3: Update config
            blocked_apps = [b for b in blocked_apps if b.get('executable', '').lower() != executable.lower()]
            self.save_blocked_applications(blocked_apps)
            
            # Step 4: Group Policy update
            self.update_group_policy()
            
            # Step 5: Restart Explorer
            explorer_restarted = self.restart_explorer()
            
            log_software_blocking("="*80)
            log_software_blocking(f"UNBLOCKING COMPLETE: {executable}")
            log_software_blocking("="*80)
            
            return {
                'success': True,
                'message': f'{executable} unblocked successfully',
                'details': {
                    'registry_removed': registry_removed,
                    'explorer_restarted': explorer_restarted
                }
            }
            
        except Exception as e:
            log_software_blocking(f"ERROR: {e}", "ERROR")
            return {'success': False, 'message': str(e)}
    
    def get_installed_applications(self):
        """Get installed applications"""
        apps = []
        seen = set()
        
        paths = [
            Path("C:/Program Files"),
            Path("C:/Program Files (x86)"),
            Path.home() / "AppData/Local/Programs"
        ]
        
        for base_path in paths:
            if not base_path.exists():
                continue
            
            try:
                for app_dir in base_path.iterdir():
                    if not app_dir.is_dir():
                        continue
                    
                    for exe_file in app_dir.rglob("*.exe"):
                        exe_name = exe_file.name.lower()
                        
                        if exe_name not in seen:
                            seen.add(exe_name)
                            apps.append({
                                'name': exe_file.stem,
                                'executable': exe_file.name,
                                'path': str(exe_file)
                            })
            except:
                continue
        
        return sorted(apps, key=lambda x: x['name'].lower())


# Initialize security agent
security_agent = SecurityAgent()

# ============================================================================
# API ROUTES
# ============================================================================

@flask_app.route('/')
def index():
    """Serve main interface"""
    return send_from_directory(WEB_DIR, 'index.html')

@flask_app.route('/health')
def test_root():
    return jsonify({'status': 'Backend is running', 'message': 'API endpoints available'})

@flask_app.route('/<path:filename>')
def serve_static(filename):
    """Serve static files"""
    return send_from_directory(WEB_DIR, filename)

# Authentication
@flask_app.route('/api/auth/login', methods=['POST'])
def login():
    """Admin authentication"""
    data = request.get_json()
    token = security_agent.generate_admin_token(data.get('username'), data.get('password'))
    if token:
        return jsonify({'success': True, 'token': token})
    return jsonify({'success': False, 'message': 'Invalid credentials'}), 401

# Antivirus Routes
@flask_app.route('/api/antivirus/scan', methods=['POST'])
def start_scan():
    """Start antivirus scan"""
    data = request.get_json()
    scan_path = data.get('path', '')
    scan_type = data.get('scan_type', 'directory')
    
    if scan_type == 'system':
        actual_path = None
    elif scan_type == 'quick_system':
        actual_path = None
    else:
        if not scan_path or not Path(scan_path).exists():
            return jsonify({'success': False, 'message': 'Invalid scan path'}), 400
        actual_path = scan_path
    
    session_id = hashlib.md5(f"{scan_path}{scan_type}{time.time()}".encode()).hexdigest()
    
    if scan_type == 'system':
        thread = threading.Thread(target=security_agent.scan_full_system, args=(session_id,))
    elif scan_type == 'quick_system':
        thread = threading.Thread(target=security_agent.scan_quick_system, args=(session_id,))
    else:
        thread = threading.Thread(target=security_agent.scan_directory, args=(actual_path, session_id))
    
    thread.start()
    
    return jsonify({'success': True, 'session_id': session_id})

@flask_app.route('/api/antivirus/status/<session_id>')
def scan_status(session_id):
    """Get scan status"""
    if session_id in SCAN_SESSIONS:
        session = SCAN_SESSIONS[session_id]
        return jsonify({
            'success': True,
            'session': {
                'session_id': session_id,
                'status': session['status'],
                'path': session['path'],
                'started_at': session['started_at'].isoformat(),
                'files_scanned': session.get('files_scanned', 0),
                'threats_found': session.get('threats_found', 0),
                'progress_percent': session.get('progress_percent', 0),
                'scan_log': session.get('scan_log', []),
                'threats': session.get('threats', []),
                'last_update': session['last_update'].isoformat()
            }
        })
    return jsonify({'success': False, 'message': 'Session not found'}), 404

@flask_app.route('/api/antivirus/cancel/<session_id>', methods=['POST'])
def cancel_scan_route(session_id):
    """Cancel scan"""
    result = security_agent.cancel_scan(session_id)
    return jsonify(result)

@flask_app.route('/api/antivirus/database-info')
def get_database_info_route():
    """Get database info"""
    info = security_agent.get_database_info()
    return jsonify({'success': True, 'database_info': info})

@flask_app.route('/api/antivirus/update-database', methods=['POST'])
def trigger_database_update():
    """Update database"""
    success = security_agent._update_virus_database()
    if success:
        return jsonify({'success': True, 'message': 'Database updated'})
    return jsonify({'success': False, 'message': 'Update failed'}), 500

# Web Blocking Routes
@flask_app.route('/api/web-blocking/urls', methods=['GET'])
def get_blocked_urls():
    """Get blocked URLs"""
    urls = security_agent.load_blocked_urls()
    return jsonify({'success': True, 'urls': urls})

@flask_app.route('/api/web-blocking/block', methods=['POST'])
def block_url_route():
    """Block URL"""
    data = request.get_json()
    url = data.get('url', '').strip()
    if not url:
        return jsonify({'success': False, 'message': 'URL required'}), 400
    
    success = security_agent.block_url(url)
    if success:
        return jsonify({'success': True, 'message': f'URL {url} blocked'})
    return jsonify({'success': False, 'message': 'Failed to block URL'}), 500

@flask_app.route('/api/web-blocking/unblock', methods=['POST'])
def unblock_url_route():
    """Unblock URL"""
    data = request.get_json()
    url = data.get('url', '').strip()
    if not url:
        return jsonify({'success': False, 'message': 'URL required'}), 400
    
    success = security_agent.unblock_url(url)
    if success:
        return jsonify({'success': True, 'message': f'URL {url} unblocked'})
    return jsonify({'success': False, 'message': 'Failed to unblock URL'}), 500

# Patch Management Routes
@flask_app.route('/api/patch-management/info')
def patch_info():
    """Get patch info"""
    info = security_agent.get_patch_info()
    return jsonify({'success': True, 'data': info})

@flask_app.route('/api/patch-management/install', methods=['POST'])
def install_patches():
    """Install patches"""
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not security_agent.verify_admin_token(token):
        return jsonify({'success': False, 'message': 'Admin required'}), 401
    
    data = request.get_json()
    result = security_agent.install_updates(data.get('update_ids', []))
    return jsonify(result)

# Software Blocking Routes (NEW)
@flask_app.route('/api/app-blocking/applications', methods=['GET'])
def get_applications():
    """Get installed applications"""
    try:
        apps = security_agent.get_installed_applications()
        return jsonify({'success': True, 'applications': apps, 'count': len(apps)})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

@flask_app.route('/api/app-blocking/block', methods=['POST'])
def block_app():
    """Block application"""
    try:
        data = request.get_json()
        result = security_agent.block_application(data.get('name', ''), data.get('executable', ''))
        return jsonify(result)
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

@flask_app.route('/api/app-blocking/unblock', methods=['POST'])
def unblock_app():
    """Unblock application"""
    try:
        data = request.get_json()
        result = security_agent.unblock_application(data.get('executable', ''))
        return jsonify(result)
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

@flask_app.route('/api/app-blocking/blocked', methods=['GET'])
def get_blocked_apps():
    """Get blocked applications"""
    try:
        apps = security_agent.get_blocked_applications()
        return jsonify({'success': True, 'count': len(apps), 'apps': apps})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

@flask_app.route('/api/app-blocking/verify/<executable>', methods=['GET'])
def verify_app_blocked(executable):
    """Verify if application is blocked"""
    try:
        apps = security_agent.get_blocked_applications()
        app = next((b for b in apps if b.get('executable', '').lower() == executable.lower()), None)
        return jsonify({'success': True, 'is_blocked': bool(app), 'details': app})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

# System Status
@flask_app.route('/api/system/status')
def system_status():
    """Get system status"""
    try:
        cpu_percent = psutil.cpu_percent(interval=1)
        memory = psutil.virtual_memory()
        disk = psutil.disk_usage('/')
        
        return jsonify({
            'success': True,
            'system': {
                'cpu_percent': cpu_percent,
                'memory_percent': memory.percent,
                'disk_percent': disk.percent,
                'processes': len(psutil.pids()),
                'timestamp': datetime.now().isoformat()
            }
        })
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)}), 500

# ============================================================================
# SCHEDULER
# ============================================================================

def run_scheduler():
    """Run scheduler"""
    print("Scheduler thread started...")
    while True:
        try:
            schedule.run_pending()
            time.sleep(1)
        except Exception as e:
            print(f"Scheduler error: {e}")
            time.sleep(5)

# ============================================================================
# MAIN
# ============================================================================

if __name__ == '__main__':
    print("Starting RiskNoX Security Agent Backend...")
    print(f"Config: {CONFIG_DIR}")
    print(f"Vendor: {VENDOR_DIR}")
    print(f"Logs: {LOGS_DIR}")
    
    WEB_DIR.mkdir(exist_ok=True)
    
    # Check ClamAV
    print("Checking ClamAV...")
    if not security_agent._check_clamav_databases():
        print("Updating virus database...")
        security_agent._update_virus_database()
    
    security_agent.setup_database_updates()
    
    # Start scheduler
    scheduler_thread = threading.Thread(target=run_scheduler, daemon=True)
    scheduler_thread.start()
    
    # Restart monitors for existing blocks
    blocked_apps = security_agent.get_blocked_applications()
    log_software_blocking(f"Loaded {len(blocked_apps)} blocked app(s)")
    for app in blocked_apps:
        if app.get('status') == 'active':
            security_agent.start_process_monitor(app.get('executable'))
    
    print("Starting Flask on port 5000...")
    flask_app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
