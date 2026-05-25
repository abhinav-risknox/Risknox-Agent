# Codebase Concerns & Technical Debt

*Last Updated: 2026-05-25*

This document outlines known issues, technical debt, security vulnerabilities, performance bottlenecks, and fragile areas in the codebase.

## 1. Security Vulnerabilities

### 1.1 SQL Injection
- **Location:** `src/manager/db/PostgresClient.cpp` in `getAuditLog()`
- **Issue:** The `agentId` parameter is directly concatenated into the SQL query (`"FROM policy_commands WHERE agent_id = '" + agentId + "' "`). If an attacker passes a malicious agent ID via the API, they can execute arbitrary SQL commands.
- **Remediation:** Replace string concatenation with parameterized queries (`PQexecParams`), which is already used in other parts of `PostgresClient.cpp`.

### 1.2 Command Injection Risk
- **Location:** `src/antivirus/AntivirusWorker.cpp` in `scanPath` handling.
- **Issue:** The API accepts a `path` parameter for antivirus scans. This path is injected directly into a command string for `clamscan.exe` (`"\" + path + "\""`). While `CreateProcessW` handles parsing, a path containing unescaped double-quotes could allow an attacker to append arbitrary command-line arguments (e.g. `--remove`) or target unintended files.
- **Remediation:** Strictly sanitize and validate incoming `path` parameters, ensuring they do not contain command line injection characters (`"`, `|`, etc.) and correspond to a valid system path.

### 1.3 Hardcoded Credentials
- **Location:** `docker-compose.yml` and `docker-compose.ec2.yml`
- **Issue:** Database and OpenSearch credentials (`POSTGRES_PASSWORD=postgres`, `OPENSEARCH_INITIAL_ADMIN_PASSWORD=Opensearch@123`) are hardcoded in the compose files.
- **Remediation:** Migrate secrets to `.env` files or a secrets manager.

### 1.4 Memory Leaks in OpenSSL Handling
- **Location:** `src/manager/server/ManagerServer.cpp` and `src/network/TcpSender.cpp`
- **Issue:** OpenSSL constructs (`SSL_CTX_new`, `BIO_new`, `X509_free`, etc.) use raw pointers and manual memory management. If an error occurs midway through setup (triggering an early `return`), resources are easily leaked.
- **Remediation:** Wrap OpenSSL objects in C++ smart pointers (`std::unique_ptr` with custom OpenSSL deleters) to guarantee cleanup on all exit paths.

## 2. Performance Issues

### 2.1 O(N*M) CPU Spike in OpenPortsCollector
- **Location:** `src/sysinfo/OpenPortsCollector.cpp`
- **Issue:** The `getProcessName()` function calls `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` to resolve the process name. This is done inside a loop for *every single open port*. On systems with many open ports, this will create thousands of process snapshots per collection cycle, spiking agent CPU.
- **Remediation:** Take a single process snapshot at the beginning of `collect()` and cache the PID-to-name mapping.

### 2.2 Unscalable Process Monitoring Threads
- **Location:** `src/appblock/SoftwareBlocker.cpp`
- **Issue:** `monitorLoop()` spins up a dedicated thread for *each* blocked application. Each thread runs a tight loop `Sleep(300ms)` and queries `CreateToolhelp32Snapshot`. Blocking multiple apps scales extremely poorly and will monopolize system resources.
- **Remediation:** Use a single monitor thread that takes one snapshot per interval and compares all running processes against the blocked application list.

### 2.3 Synchronous Hashing blocks USN Polling Thread
- **Location:** `src/fim/FimMonitor.cpp`
- **Issue:** When a file is created or modified, `processChange` is triggered, which synchronously calculates the SHA-256 hash (`scanner_->scanFile`). If thousands of files change rapidly (e.g., git pull), hashing them synchronously on the USN Journal polling thread will block the thread, potentially causing dropped USN events or huge delays.
- **Remediation:** Offload file hashing to a separate worker thread or thread pool.

### 2.4 Synchronous COM Calls Block Thread Joining
- **Location:** `src/patch/PatchManager.cpp`
- **Issue:** `scanForUpdates()` and `installUpdates()` invoke blocking Windows Update Agent COM APIs. There is no cancellation mechanism passed to WUA. When `PatchManager::stop()` is called, `scanThread_.join()` may hang indefinitely if an update scan is in progress, preventing graceful shutdown of the agent.

## 3. Fragile Code & Technical Debt

### 3.1 Use-After-Free Crash in SoftwareBlocker
- **Location:** `src/appblock/SoftwareBlocker.cpp`
- **Issue:** In `stopProcessMonitor`, if a thread is joinable, it is immediately detached (`it->second.detach()`). The detached thread continues running its `monitorLoop` and accesses member variables like `running_` and `mutex_`. If the `SoftwareBlocker` instance is destroyed, the detached thread will crash accessing freed memory.
- **Remediation:** Properly signal threads to exit (e.g. using a stop token or flag) and `join()` them rather than detaching.

### 3.2 Out-of-sync Database Schema (Missing `initiated_by`)
- **Location:** `src/manager/db/PostgresClient.cpp` & `db/migrations/schema.sql`
- **Issue:** `schema.sql` is missing the `initiated_by` column on the `policy_commands` table. Because of this, `ManagerServer.cpp` (line 688) has a `TODO` dropping the `initiatedBy` parameter, and `PostgresClient.cpp` (line 1148) logs a fallback warning when trying to record module commands.

### 3.3 Missing Hardware Telemetry Implementation
- **Location:** `src/agent/network/TlsSender.cpp` (line 425)
- **Issue:** Currently contains a `TODO: Add real CPU/Mem info if available`. The agent reports placeholder or null values for hardware telemetry.

### 3.4 Dead / Legacy Code
- **Location:** `backend_server_complete_with_blocking.py`
- **Issue:** A standalone Python backend exists in the root folder, which seems disconnected from the actual C++ `manager` system. It clutters the workspace.
