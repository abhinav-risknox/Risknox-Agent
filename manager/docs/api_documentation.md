# ResolutePulse REST API Reference

This document provides a production-grade specification for the ResolutePulse Manager REST API. All requests and responses are transmitted over `application/json`.

## Base Configuration
- **Base URL:** `http(s)://<manager_ip>:<port>`
- **CORS:** Global `Access-Control-Allow-Origin: *` is enforced. Preflight `OPTIONS` requests are handled automatically with a `204 No Content`.
- **Authentication:** Most endpoints require a valid Bearer token.
  ```http
  Authorization: Bearer <token>
  ```
- **Error Handling:** Errors return a `400 Bad Request`, `401 Unauthorized`, or `404 Not Found` with a standard JSON structure:
  ```json
  { "error": "Descriptive error message" }
  ```

---

## Authentication & Health

### `POST /api/auth/login`
**Description:** Authenticates an operator and issues a session token.
**Auth Required:** No

#### Request
- **Body (`application/json`):**
  ```json
  {
    "username": "admin",
    "password": "SecurePassword123!"
  }
  ```

#### Response
- **Status `200 OK`:**
  ```json
  {
    "token": "4f1b7a2de98f7...",
    "username": "admin",
    "expires_in": 28800
  }
  ```

### `GET /api/health`
**Description:** Returns the system health status, database connectivity, and server state.
**Auth Required:** No

#### Request
*(No parameters or body)*

#### Response
- **Status `200 OK`:**
  ```json
  {
    "status": "healthy",
    "db_connected": true,
    "server_running": true,
    "timestamp": "2026-06-16T13:30:00Z"
  }
  ```

---

## Agent Registry & Status

### `GET /api/agents`
**Description:** Retrieves a list of all registered endpoint agents.
**Auth Required:** Yes

#### Request
*(No parameters or body)*

#### Response
- **Status `200 OK`:**
  ```json
  {
    "count": 1,
    "agents": [
      {
        "agent_id": "DESKTOP-ABC1234",
        "hostname": "DESKTOP-ABC1234",
        "os_type": "Windows",
        "os_version": "10.0.19045",
        "agent_version": "1.0.0",
        "status": "active",
        "ip_address": "192.168.1.50",
        "registered_at": "2026-06-15T10:00:00Z",
        "last_seen_at": "2026-06-16T13:30:00Z",
        "online": true
      }
    ]
  }
  ```

### `DELETE /api/agents/{agent_id}`
**Description:** Removes a registered agent from the system.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Unique identifier for the agent.

#### Response
- **Status `200 OK`:**
  ```json
  {
    "success": true,
    "message": "Agent removed successfully"
  }
  ```
- **Status `404 Not Found`:**
  ```json
  { "error": "Agent not found" }
  ```

### `GET /api/agents/{agent_id}`
**Description:** Retrieves detailed information about a specific agent, including its active license.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Unique identifier for the agent.

#### Response
- **Status `200 OK`:**
  ```json
  {
    "agent_id": "DESKTOP-ABC1234",
    "hostname": "DESKTOP-ABC1234",
    "os_type": "Windows",
    "os_version": "10.0.19045",
    "agent_version": "1.0.0",
    "status": "active",
    "cert_serial": "01:23:45:67",
    "ip_address": "192.168.1.50",
    "registered_at": "2026-06-15T10:00:00Z",
    "last_seen_at": "2026-06-16T13:30:00Z",
    "online": true,
    "license": {
      "type": "Enterprise",
      "valid_from": "2026-01-01T00:00:00Z",
      "valid_until": "2027-01-01T00:00:00Z"
    }
  }
  ```

### `GET /api/agents/{agent_id}/status`
**Description:** Retrieves the latest active status reports (e.g., `module_status`) pushed by the agent.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Unique identifier for the agent.

#### Response
- **Status `200 OK`:**
  ```json
  {
    "agent_id": "DESKTOP-ABC1234",
    "online": true,
    "reports": [
      {
        "report_type": "module_status",
        "report_data": { 
           "web_blocking": { "status": "running" },
           "software_blocking": { "status": "running" }
        },
        "created_at": "2026-06-16T13:25:00Z"
      }
    ]
  }
  ```

---

## Core Command Subsystem

### `POST /api/agents/{agent_id}/module-command`
**Description:** Dispatches a raw, asynchronous module command to the agent.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Unique identifier for the agent.
- **Body (`application/json`):**
  ```json
  {
    "verb": "diagnostics",
    "params": { 
      "detail": "full" 
    }
  }
  ```

#### Response
- **Status `200 OK`:**
  ```json
  {
    "command_id": "DESKTOP-ABC1234-diagnostics-1686900000-abcd1234",
    "agent_id": "DESKTOP-ABC1234",
    "verb": "diagnostics",
    "status": "queued",
    "initiated_by": "admin"
  }
  ```

### `GET /api/commands/module`
**Description:** Retrieve a list of dispatched module commands.
**Auth Required:** Yes

#### Request
- **Query Parameters:**
  - `agent_id` (string, optional): Filter by a specific agent.
  - `limit` (integer, optional): Number of commands to return (default: 50).
  - `offset` (integer, optional): Pagination offset (default: 0).

#### Response
- **Status `200 OK`:**
  ```json
  {
    "count": 10,
    "commands": [
      {
        "command_id": "DESKTOP-ABC1234-diagnostics-1686900000-abcd1234",
        "agent_id": "DESKTOP-ABC1234",
        "verb": "diagnostics",
        "status": "queued",
        "initiated_by": "admin"
      }
    ]
  }
  ```

### `POST /api/agents/{agent_id}/policy`
**Description:** Push a new policy update configuration to an agent.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Unique identifier for the agent.
- **Body (`application/json`):**
  ```json
  {
    "policy_type": "antivirus",
    "policy_data": { 
      "action": "quick_scan"
    }
  }
  ```

#### Response
- **Status `200 OK`:**
  ```json
  {
    "command_id": "DESKTOP-ABC1234-antivirus-1686900000-abcd1234",
    "agent_id": "DESKTOP-ABC1234",
    "policy_type": "antivirus",
    "status": "queued",
    "initiated_by": "admin"
  }
  ```

### `GET /api/commands/policy`
**Description:** Retrieve a list of pushed policy commands.
**Auth Required:** Yes

#### Request
- **Query Parameters:**
  - `agent_id` (string, optional): Filter by a specific agent.
  - `limit` (integer, optional): Number of commands to return (default: 50).
  - `offset` (integer, optional): Pagination offset (default: 0).

#### Response
- **Status `200 OK`:**
  ```json
  {
    "count": 5,
    "commands": [
      {
        "command_id": "DESKTOP-ABC1234-antivirus-1686900000-abcd1234",
        "agent_id": "DESKTOP-ABC1234",
        "policy_type": "antivirus",
        "status": "queued",
        "initiated_by": "admin"
      }
    ]
  }
  ```

### `GET /api/commands/{command_id}`
**Description:** Polling endpoint to retrieve the execution status and return payload of any command (module, policy, or endpoint management).
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `command_id` (string): The command identifier returned when the command was dispatched.

#### Response
- **Status `200 OK`:**
  ```json
  {
    "type": "module",
    "id": 142,
    "agent_id": "DESKTOP-ABC1234",
    "command_id": "DESKTOP-ABC1234-inventory_collect-1686900000-abcd",
    "verb": "inventory_collect",
    "params": {},
    "status": "acked",
    "ack_status": "success",
    "result_payload": { "architecture": "x64", "build": "19045" },
    "created_at": "2026-06-16T13:30:00Z"
  }
  ```

### `GET /api/audit-log`
**Description:** Retrieves the centralized audit log, mapping dispatched commands to the operators who initiated them.
**Auth Required:** Yes

#### Request
- **Query Parameters:**
  - `agent_id` (string, optional): Filter by agent.
  - `limit` (integer, optional): Pagination limit (default: 100).
  - `offset` (integer, optional): Pagination offset (default: 0).

#### Response
- **Status `200 OK`:**
  ```json
  {
    "count": 1,
    "entries": [
      {
         "id": 1,
         "agent_id": "DESKTOP-ABC1234",
         "command_id": "...",
         "operator_name": "admin",
         "verb": "diagnostics",
         "created_at": "2026-06-16T13:30:00Z"
      }
    ]
  }
  ```

---

## Native Endpoint Management

These routes provide native REST abstractions for managing operating system endpoints. Under the hood, they automatically queue an asynchronous module command. The response from all of these endpoints is a `command_id` indicating the job is queued. Use `GET /api/commands/{command_id}` to retrieve the actual result payload once the agent finishes execution.

### System Inventory

### `GET /api/agents/{agent_id}/endpoint/inventory`
**Description:** Dispatches an `inventory_collect` command to perform a deep hardware and software scan.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 
- **Eventual `result_payload` (from `GET /api/commands/{command_id}`):**
  ```json
  {
    "architecture": "x64",
    "build": "19045",
    "version": "10.0",
    "num_processors": "16",
    "ip_addresses": [ { "adapter": "Ethernet", "ip": "192.168.1.50", "version": "IPv4" } ],
    "logical_disks": [ { "drive": "C:", "type": "Local", "total_bytes": 10000000, "free_bytes": 5000000 } ]
  }
  ```

### `POST /api/agents/{agent_id}/endpoint/users/{username}/unlock`
**Description:** Dispatches a `user_unlock` command to unlock a locked local user account.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### Group Management

### `GET /api/agents/{agent_id}/endpoint/users`
**Description:** Dispatches a `user_list` command to retrieve all local OS users.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 
- **Eventual `result_payload`:**
  ```json
  [ 
    { 
      "username": "admin", 
      "full_name": "Administrator", 
      "is_enabled": true, 
      "is_locked": false, 
      "password_required": true, 
      "password_expires": false 
    } 
  ]
  ```

### `POST /api/agents/{agent_id}/endpoint/users`
**Description:** Dispatches a `user_create` command to create a new local OS user.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
- **Body (`application/json`):**
  ```json
  {
    "username": "newuser",
    "password": "SecurePassword123!",
    "full_name": "New Employee" 
  }
  ```

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `DELETE /api/agents/{agent_id}/endpoint/users/{username}`
**Description:** Dispatches a `user_delete` command to remove a local OS user.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `POST /api/agents/{agent_id}/endpoint/users/{username}/enable`
**Description:** Dispatches a `user_enable` command to re-enable a suspended local user.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `POST /api/agents/{agent_id}/endpoint/users/{username}/disable`
**Description:** Dispatches a `user_disable` command to suspend a local user.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `POST /api/agents/{agent_id}/endpoint/users/{username}/password`
**Description:** Dispatches a `user_password_change` command to forcefully change a local user's password.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `username` (string): Target user account name.
- **Body (`application/json`):**
  ```json
  {
    "password": "NewSecurePassword123!"
  }
  ```

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### Group Management

### `GET /api/agents/{agent_id}/endpoint/groups`
**Description:** Dispatches a `group_list` command to retrieve all local OS user groups.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 
- **Eventual `result_payload`:**
  ```json
  [ { "groupname": "Administrators" }, { "groupname": "Users" } ]
  ```

### `POST /api/agents/{agent_id}/endpoint/groups/{groupname}/users/{username}`
**Description:** Dispatches a `group_add_user` command to add a local user to a local group.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `groupname` (string): Target group name.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `DELETE /api/agents/{agent_id}/endpoint/groups/{groupname}/users/{username}`
**Description:** Dispatches a `group_remove_user` command to remove a local user from a local group.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `groupname` (string): Target group name.
  - `username` (string): Target user account name.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### Session Management

### `GET /api/agents/{agent_id}/endpoint/sessions`
**Description:** Dispatches a `session_list` command to query active remote and local user sessions on the OS.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 
- **Eventual `result_payload`:**
  ```json
  [ { "session_id": 1, "station_name": "Console", "username": "admin", "state": "Active" } ]
  ```

### `POST /api/agents/{agent_id}/endpoint/sessions/{session_id}/logoff`
**Description:** Dispatches a `session_logoff` command to force close a user's session.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `session_id` (integer): ID of the session to terminate.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `POST /api/agents/{agent_id}/endpoint/sessions/{session_id}/disconnect`
**Description:** Dispatches a `session_disconnect` command to safely disconnect an RDP session without closing it.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
  - `session_id` (integer): ID of the session to disconnect.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### `POST /api/agents/{agent_id}/endpoint/workstation/lock`
**Description:** Dispatches a `workstation_lock` command to lock the current active workstation session.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

### OS Password Policy

### `GET /api/agents/{agent_id}/endpoint/password-policy`
**Description:** Dispatches a `password_policy_get` command to fetch the OS-level local password policy requirements.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.

#### Response
- **Status `200 OK`:** Returns standard queued command response. 
- **Eventual `result_payload`:**
  ```json
  {
    "min_length": 8,
    "max_age_days": 90,
    "min_age_days": 0,
    "history_length": 5
  }
  ```

### `POST /api/agents/{agent_id}/endpoint/password-policy`
**Description:** Dispatches a `password_policy_set` command to modify the OS-level local password policy.
**Auth Required:** Yes

#### Request
- **Path Parameters:**
  - `agent_id` (string): Target agent.
- **Body (`application/json`):**
  *(All fields are optional; omitting a field or passing `-1` retains the existing value)*
  ```json
  {
    "min_length": 12,
    "max_age_days": 60,
    "min_age_days": 1,
    "history_length": 10
  }
  ```

#### Response
- **Status `200 OK`:** Returns standard queued command response. 

---

## Settings

### `GET /api/settings`
**Description:** Retrieves the current server settings, including the maximum number of agents allowed and the current agent count.
**Auth Required:** Yes

#### Request
*(No parameters or body)*

#### Response
- **Status `200 OK`:**
  ```json
  {
    "max_agents": 100,
    "current_agent_count": 5
  }
  ```

### `PUT /api/settings`
**Description:** Updates server settings. Currently supports modifying the maximum number of allowed agents.
**Auth Required:** Yes

#### Request
- **Body (`application/json`):**
  ```json
  {
    "max_agents": 200
  }
  ```

#### Response
- **Status `200 OK`:**
  ```json
  {
    "max_agents": 200,
    "current_agent_count": 5,
    "success": true
  }
  ```
- **Status `400 Bad Request`:**
  ```json
  { "error": "max_agents must be >= 0" }
  ```

---

## Supported Agent Operations

These are the actual command types supported by the `Agent` component that you should pass in as the `verb` (for `module-command`) or `policy_type` (for `policy` command).

### Supported Module Command Verbs (`verb`)
- `collector_start`: Resume event collection & batch sender
- `collector_stop`: Pause event collection & batch sender
- `fim_start`: Resume FIM monitoring
- `fim_stop`: Pause FIM monitoring
- `worker_restart`: Stop & re-spawn all persistent worker subprocesses
- `status_request`: Force an immediate status flush and STATUS_REPORT
- `diagnostics`: Return live counters & last log lines
- `config_get`: Return the active JSON config or one section
- `config_push`: Accept a new JSON config section, persist, and apply
- `agent_restart`: Schedule a graceful restart
- `av_update`: Trigger on-demand freshclam update via rp-antivirus
- `av_version`: Query ClamAV DB metadata via rp-antivirus

### Supported Policy Command Types (`policy_type`)

All policy commands are dispatched via `POST /api/agents/{agent_id}/policy`.  The `policy_type` field selects the target module, and `policy_data` carries the action-specific payload.

---

#### `web_blocking` — Web URL Blocking

Pushes web blocking rules to the `rp-webblock` worker. URLs are blocked by redirecting them to `127.0.0.1` in the OS hosts file.

**Block a single URL:**
```json
{
  "policy_type": "web_blocking",
  "policy_data": {
    "action": "block",
    "url": "https://www.malicious-site.com"
  }
}
```

**Unblock a single URL:**
```json
{
  "policy_type": "web_blocking",
  "policy_data": {
    "action": "unblock",
    "url": "malicious-site.com"
  }
}
```

**Replace all blocked URLs (full sync):**
```json
{
  "policy_type": "web_blocking",
  "policy_data": {
    "action": "replace",
    "urls": [
      { "url": "gambling-site.com", "status": "active" },
      { "url": "phishing-domain.net", "status": "active" }
    ]
  }
}
```

> **Notes:**
> - URL protocols (`http://`, `https://`) and trailing slashes are stripped automatically.
> - Both `example.com` and `www.example.com` variants are blocked.
> - DNS cache is flushed automatically after every change.

---

#### `software_blocking` — Application Blocking

Pushes software blocking rules to the `rp-softblock` worker. Blocked executables are prevented from launching via Windows IFEO (Image File Execution Options) registry keys, and any running instances are terminated immediately.

**Block an application:**
```json
{
  "policy_type": "software_blocking",
  "policy_data": {
    "action": "block",
    "name": "Tor Browser",
    "executable": "tor.exe"
  }
}
```

**Unblock an application:**
```json
{
  "policy_type": "software_blocking",
  "policy_data": {
    "action": "unblock",
    "executable": "tor.exe"
  }
}
```

**Replace all blocked applications (full sync):**
```json
{
  "policy_type": "software_blocking",
  "policy_data": {
    "action": "replace",
    "apps": [
      { "name": "BitTorrent", "executable": "bittorrent.exe" },
      { "name": "Tor Browser", "executable": "tor.exe" }
    ]
  }
}
```

> **Notes:**
> - Executable matching is case-insensitive and works with or without the `.exe` extension.
> - Running instances are force-terminated via both Win32 API and `taskkill` fallback.
> - A background process monitor continuously watches for re-launch attempts.

---

#### `patch` — Windows Patch Management

Triggers a background patch scan via the `rp-patch` worker. Uses the Windows Update Agent (WUA) API to discover and optionally install missing updates.

**Trigger an immediate patch scan:**
```json
{
  "policy_type": "patch",
  "policy_data": {
    "trigger_scan": true
  }
}
```

**Update patch scan configuration:**
```json
{
  "policy_type": "patch",
  "policy_data": {
    "auto_scan": true,
    "scan_interval_hours": 12,
    "auto_install": false,
    "exclude_kbs": ["KB5001234", "KB5005678"]
  }
}
```

**Install specific updates by Update ID:**
```json
{
  "policy_type": "patch",
  "policy_data": {
    "install_update_ids": [
      "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
    ]
  }
}
```

> **Notes:**
> - Patch scans can take several minutes; they run on a background thread and do not block the agent.
> - `exclude_kbs` allows specific KB articles to be skipped during both scan and install.
> - All `policy_data` fields are optional; omitting a field retains the current setting.
> - Scan results are reported back via the `patch_scan` status report.

---

#### `antivirus` — Antivirus Scanning & Management

Triggers antivirus actions via the `rp-antivirus` worker (backed by ClamAV). Supports on-demand scanning, definition updates, and database queries.

**Quick scan (specific path):**
```json
{
  "policy_type": "antivirus",
  "policy_data": {
    "action": "quick_scan",
    "path": "C:\\Users\\Public\\Downloads"
  }
}
```

**Quick scan (multiple paths):**
```json
{
  "policy_type": "antivirus",
  "policy_data": {
    "action": "quick_scan",
    "paths": [
      "C:\\Users\\Public\\Downloads",
      "D:\\SharedFolder"
    ]
  }
}
```

**Full system scan:**
```json
{
  "policy_type": "antivirus",
  "policy_data": {
    "action": "full_scan"
  }
}
```

**Update virus definitions (freshclam):**
```json
{
  "policy_type": "antivirus",
  "policy_data": {
    "action": "update_definitions"
  }
}
```

**Query ClamAV database info:**
```json
{
  "policy_type": "antivirus",
  "policy_data": {
    "action": "database_info"
  }
}
```

> **Notes:**
> - `quick_scan` and `full_scan` are serialized — only one scan runs at a time. Additional paths arriving during a scan are batched and processed when the current scan finishes.
> - `update_definitions` and `database_info` run immediately on isolated pipes and do not block scans.
> - Detected threats trigger a desktop notification (if `ThreatNotification.exe` is available) and are moved to a quarantine directory.
> - Scan results are reported back via the `av_scan` status report.
