# Testing Patterns & Frameworks

**Date:** 2026-05-25

The repository employs a lightweight, dependency-minimizing testing philosophy, favoring custom assertions and native tools over heavy, monolithic testing frameworks. 

## 1. C++ Agent Testing (Unit & Integration)

**Location:** `tests/*.cpp`

### Framework and Execution
*   **Custom Test Executables:** There is no centralized testing framework (like Google Test or Catch2). Instead, each test file (e.g., `test_agent_handler.cpp`, `test_agent_tls.cpp`) contains its own `main()` function and compiles to a standalone binary (`test_agent_handler.exe`).
*   **Assertions:** Tests rely heavily on standard `<cassert>`. A test executable simply crashes on failure and exits with `0` on success.
*   **Hybrid Testing:** Many binaries combine both pure unit tests (testing class logic locally) and localized integration tests (spinning up local TCP sockets or hitting real file system directories).

### Mocking Strategy
*   **C-Level Stubs:** External C-based dependencies are mocked by redefining the library's function signatures directly in the test files. 
    *   *Example:* In `test_agent_handler.cpp`, the `libpq` PostgreSQL library is mocked by implementing custom versions of `PQconnectdb`, `PQexec`, and `PQfinish` that return hardcoded `pg_conn` and `pg_result` structures. This effectively fakes database interactions without requiring a mocking framework.

## 2. Python Integration Testing

**Location:** `tests/*.py` (e.g., `test_av_scan.py`, `test_tcp_cmd.py`)

### Execution
*   **Scripted IPC Tests:** Python scripts are used as end-to-end integration drivers for compiled C++ binaries. They use `subprocess.Popen` to spawn the agent executables (like `rp-antivirus.exe`).
*   **Communication:** These scripts connect to the spawned processes via IPC channels (e.g., Windows Named Pipes: `\\.\pipe\rp-antivirus`) and send/receive structured JSON commands. 
*   **Assertions:** Success is determined procedurally using standard Python conditionals and standard output. No `pytest` or `unittest` decorators are used.

## 3. JavaScript / Node.js Testing (Pulse)

**Location:** `Pulse/backend/src/tests/` and `Pulse/frontend/src/`

### Backend (Node.js)
*   **Custom Scripts:** Similar to the Python testing strategy, the Node.js backend uses standalone `.js` scripts (like `testQueues.js`) to perform end-to-end smoke testing of critical infrastructure components (such as Redis connectivity, BullMQ queue dispatch, and Worker processing).
*   **Assertions:** Success/Failure is printed to the console using custom formatting helpers (e.g., `pass()`, `fail()`).

### Frontend (React)
*   **Jest & Testing Library:** The React frontend uses a standard Create React App testing setup. It relies on Jest and `@testing-library/react` (e.g., `App.test.js`) to assert component rendering state.

## Summary of Test Strategy

*   **Avoids bloat:** Relies on compiler-native asserts (`<cassert>`) and procedural script validation.
*   **E2E emphasis:** Many tests immediately test IPC, network protocols, and real binary interactions rather than artificially isolated units.
