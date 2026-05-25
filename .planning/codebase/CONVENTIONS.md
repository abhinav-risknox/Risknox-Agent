# Codebase Conventions

**Date:** 2026-05-25

This document outlines the coding conventions, naming standards, and architectural patterns observed across the repository, divided by language and ecosystem.

## 1. C++ (Core Agent & Manager)

The primary agent (`src/`) and manager codebases are written in modern C++.

### Code Style and Naming
*   **Namespaces:** Core logic is wrapped in the `ResolutePulse` namespace to prevent symbol collisions.
*   **Classes & Structs:** PascalCase (e.g., `Agent`, `AgentHandler`, `TlsSender`).
*   **Methods & Functions:** camelCase (e.g., `initialize()`, `performRegistration()`).
*   **Member Variables:** Postfixed with an underscore `_` (e.g., `configPath_`, `agentPhase_`, `queue_`).
*   **Constants & Macros:** UPPER_CASE for preprocessor definitions and global constants.
*   **Includes:** Local/project includes use quotes and are grouped together (e.g., `#include "utils/Logger.h"`). System and library includes use angle brackets and appear below project includes (e.g., `#include <openssl/ssl.h>`, `#include <iostream>`).

### Patterns & Memory Management
*   **Smart Pointers:** `std::unique_ptr` and `std::make_unique` are used extensively to manage lifecycles and avoid memory leaks (e.g., `queue_ = std::make_unique<EventQueue>(...)`).
*   **JSON Handling:** Heavy reliance on `nlohmann/json.hpp` for configuration parsing and IPC messaging.

### Error Handling
*   **Boolean Returns:** Primary initialization and execution paths prefer returning boolean `true`/`false` to signal success instead of throwing runtime exceptions.
*   **Extensive Logging:** `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, and `LOG_CRITICAL` macros are heavily used to capture context around failures. Exceptions are generally caught at lower layers or completely avoided in favor of control flow branching.

## 2. Python (Backend & Scripting)

Python is used for the standalone backend server (`backend_server_complete_with_blocking.py`) and various test scripts.

### Code Style and Naming
*   **PEP-8 Inspired:** Functions and variables use `snake_case` (e.g., `safe_print`, `log_software_blocking`).
*   **Constants:** `UPPER_CASE` at the module level for configuration constants and global state (e.g., `ACTIVE_MONITORS`, `SOFTWARE_BLOCKING_LOG`).

### Patterns & Error Handling
*   **Defensive Programming:** Extensive use of `try/except` blocks (often swallowing non-critical exceptions) to maintain server uptime.
*   **Encoding Safety:** Custom output wrappers (`safe_print()`, `safe_str()`) guarantee ASCII-safe output, preventing `UnicodeEncodeError` crashes on Windows terminals.

## 3. JavaScript / Node.js (Pulse Stack)

The `Pulse/` directory contains a full-stack JavaScript application (React + Node.js).

### Code Style
*   **Frontend (React):** Uses ES6 imports (`import/export`), JSX syntax, and standard component-based architectures.
*   **Backend (Node.js):** Uses CommonJS modules (`require/module.exports`) and is built around Express and BullMQ. Environment variables (`dotenv`) configure state.
