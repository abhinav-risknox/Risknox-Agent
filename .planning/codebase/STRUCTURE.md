# Codebase Structure

**Date:** 2026-05-25

## 1. Directory Layout

*   **`/src/`**: The unified C++ source directory containing code for the Agent, Manager, and Workers.
    *   **`/src/manager/`**: Backend Manager server code, including the REST API, Agent Registry, Database Client, and Certificate Authority logic.
    *   **`/src/agent/`**: Agent-specific networking, focusing on Manager registration and mTLS communication.
    *   **`/src/workers/`**: Entry points and specialized logic for the micro-process workers (Patching, App Blocking, Web Blocking, Antivirus).
    *   **`/src/common/`**: Shared protocol definitions and structures used by both the Agent and the Manager.
    *   **`/src/{module}/`**: Core Agent feature modules (e.g., `/fim`, `/sysinfo`, `/logtailer`, `/ipc`, `/policy`, `/collector`).
*   **`/manager/dashboard/`**: The frontend React/Vite/Tailwind web application for managing the system.
*   **`/thirdparty/`**: External C++ dependencies and libraries (e.g., `sqlite3`).
*   **`/tests/`**: C++ test executables and suites validating Agent and Manager components.
*   **`/docs/`**: Project documentation.
*   **`/deploy/`, `/fluent-bit/`, `/data-prepper/`**: Infrastructure configuration for deploying the Manager backend, databases, and the telemetry ingestion pipeline.

## 2. Key Locations

*   **`CMakeLists.txt` (Root)**: The central build definition file. It configures compilation targets for the main Agent executable (`ResolutePulse`), the Manager executable (`ResolutePulseManager`), the individual Worker executables (`rp-softblock`, `rp-webblock`, etc.), and the test suite.
*   **`config.json`**: The default configuration blueprint for the Agent, defining log channels, FIM paths, subsystem toggles, and pipeline connection details.
*   **`fluent-bit.conf`**: Configuration for Fluent Bit detailing how to receive TCP events from the Agent and route them to Data Prepper.
*   **`docker-compose.yml`**: Primary orchestrator file for spinning up the backend services (PostgreSQL, Data Prepper, Manager Server).
*   **`installer.iss`**: Inno Setup script responsible for packaging the Windows Agent executables and configurations into an installable executable.

## 3. Naming Conventions

*   **C++ Source Files:** Uses PascalCase for class files and headers (e.g., `Agent.cpp`, `SystemInfoCollector.cpp`, `ConfigManager.h`).
*   **Executable Targets:**
    *   Main Agent: `ResolutePulse.exe`
    *   Backend Manager: `ResolutePulseManager.exe`
    *   Workers: Prefix with `rp-` indicating they are Resolute Pulse micro-processes (e.g., `rp-patch.exe`, `rp-antivirus.exe`, `rp-webblock.exe`).
*   **React Frontend:** Standard React conventions with `PascalCase.tsx` for components and lowercase/kebab-case for utility files.
