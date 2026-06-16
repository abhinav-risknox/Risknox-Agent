# ResolutePulse Detailed REST API Documentation

This document provides a comprehensive technical reference for the ResolutePulse Manager REST API (`RestApi.cpp`), including exact request structures, expected parameters, and response schemas.

## Base Configuration
- **CORS**: All endpoints return `Access-Control-Allow-Origin: *` and respond 204 to `OPTIONS`.
- **Authentication**: Unless noted, all requests require a valid Bearer token in the header:
  `Authorization: Bearer <token>`
- **Content-Type**: Must be `application/json` for requests. Responses are `application/json`.

---

## Authentication & Health

### `POST /api/auth/login`
Authenticates an operator and returns a token.
- **Auth Required**: No
- **Request Body**:
  ```json
  {
    "username": "admin",
    "password": "password"
  }
  ```
- **Response** (200 OK):
  ```json
  {
    "token": "4f1b7a2d...",
    "username": "admin",
    "expires_in": 28800
  }
  ```
- **Errors**: `401 Unauthorized` for invalid credentials.

### `GET /api/health`
Check system status without authentication.
- **Auth Required**: No
- **Response** (200 OK):
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
List all registered agents and their current status.
- **Response** (200 OK):
  ```json
  {
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
    ],
    "count": 1
  }
  ```

### `GET /api/agents/{agent_id}`
Get detailed information about a specific agent, including its active license.
- **Response** (200 OK):
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
    "registered_at": "...",
    "last_seen_at": "...",
    "online": true,
    "license": {
      "type": "Enterprise",
      "valid_from": "2026-01-01T00:00:00Z",
      "valid_until": "2027-01-01T00:00:00Z"
    }
  }
  ```

### `GET /api/agents/{agent_id}/status`
Retrieve the latest 20 health/status reports pushed by the agent.
- **Response** (200 OK):
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
Manually dispatch a raw module command to the agent.
- **Request Body**:
  ```json
  {
    "verb": "diagnostics",
    "params": { 
      "detail": "full" 
    }
  }
  ```
- **Response** (200 OK):
  ```json
  {
    "command_id": "DESKTOP-ABC1234-diagnostics-1686900000-abcd1234",
    "agent_id": "DESKTOP-ABC1234",
    "verb": "diagnostics",
    "status": "queued",
    "initiated_by": "admin"
  }
  ```

### `GET /api/commands/{command_id}`
Unified lookup for polling the result of any dispatched command (module or policy).
- **Response** (200 OK) for a completed command:
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
    "result_payload": { ... json payload from agent ... },
    "created_at": "2026-06-16T13:30:00Z"
  }
  ```

---

## Native Endpoint Management

These endpoints wrap `handleEndpointCommand`. They translate standard REST routes into asynchronous module commands automatically.

### System Inventory
#### `GET /api/agents/{agent_id}/endpoint/inventory`
- **Translates to**: verb `inventory_collect`
- **Request Body**: None
- **Result Payload Structure**:
  ```json
  {
    "architecture": "x64",
    "build": "19045",
    "version": "10.0",
    "num_processors": "16",
    "ip_addresses": [ { "adapter": "Ethernet", "ip": "192.168.1.50", "version": "IPv4" } ],
    "logical_disks": [ { "drive": "C:", "type": "Local", "total_bytes": 100000, "free_bytes": 50000 } ]
  }
  ```

### User Management
#### `GET /api/agents/{agent_id}/endpoint/users`
- **Translates to**: verb `user_list`
- **Result Payload Structure**: `[ { "username": "admin", "full_name": "Administrator", "is_enabled": true, "is_locked": false, "password_required": true, "password_expires": false } ]`

#### `POST /api/agents/{agent_id}/endpoint/users`
- **Translates to**: verb `user_create`
- **Request Body**:
  ```json
  {
    "username": "newuser",
    "password": "SecurePassword123!",
    "full_name": "New Employee" 
  }
  ```

#### `DELETE /api/agents/{agent_id}/endpoint/users/{username}`
- **Translates to**: verb `user_delete`
- **Request Body**: None (Username passed in URL)

#### `POST /api/agents/{agent_id}/endpoint/users/{username}/enable`
- **Translates to**: verb `user_enable`

#### `POST /api/agents/{agent_id}/endpoint/users/{username}/disable`
- **Translates to**: verb `user_disable`

#### `POST /api/agents/{agent_id}/endpoint/users/{username}/password`
- **Translates to**: verb `user_password_change`
- **Request Body**:
  ```json
  {
    "password": "NewSecurePassword123!"
  }
  ```

### Group Management
#### `GET /api/agents/{agent_id}/endpoint/groups`
- **Translates to**: verb `group_list`
- **Result Payload Structure**: `[ { "groupname": "Administrators" }, { "groupname": "Users" } ]`

#### `POST /api/agents/{agent_id}/endpoint/groups/{groupname}/users/{username}`
- **Translates to**: verb `group_add_user`
- **Request Body**: None (Both variables extracted from URL)

#### `DELETE /api/agents/{agent_id}/endpoint/groups/{groupname}/users/{username}`
- **Translates to**: verb `group_remove_user`
- **Request Body**: None (Both variables extracted from URL)

### Session Management
#### `GET /api/agents/{agent_id}/endpoint/sessions`
- **Translates to**: verb `session_list`
- **Result Payload Structure**: `[ { "session_id": 1, "station_name": "Console", "username": "admin", "state": "Active" } ]`

#### `POST /api/agents/{agent_id}/endpoint/sessions/{session_id}/logoff`
- **Translates to**: verb `session_logoff`
- **Request Body**: None (Session ID extracted from URL as integer)

#### `POST /api/agents/{agent_id}/endpoint/sessions/{session_id}/disconnect`
- **Translates to**: verb `session_disconnect`
- **Request Body**: None (Session ID extracted from URL as integer)

### Password Policy
#### `GET /api/agents/{agent_id}/endpoint/password-policy`
- **Translates to**: verb `password_policy_get`
- **Result Payload Structure**:
  ```json
  {
    "min_length": 8,
    "max_age_days": 90,
    "min_age_days": 0,
    "history_length": 5
  }
  ```

#### `POST /api/agents/{agent_id}/endpoint/password-policy`
- **Translates to**: verb `password_policy_set`
- **Request Body** (All fields optional; pass `-1` or omit to keep current value):
  ```json
  {
    "min_length": 12,
    "max_age_days": 60,
    "min_age_days": 1,
    "history_length": 10
  }
  ```
