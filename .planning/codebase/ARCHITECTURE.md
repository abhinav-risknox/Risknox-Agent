# Codebase Architecture

**Date:** 2026-05-25

## 1. Architectural Pattern
The system employs a **Client-Server Architecture** augmented with **Micro-processes (Workers)** on the client side to ensure stability, security, and fault tolerance.

*   **Server (Manager):** A centralized C++ backend (`ResolutePulseManager`) that handles agent registration, issues mTLS certificates (acting as a Certificate Authority), maintains state in a PostgreSQL database, and exposes a REST API for the dashboard.
*   **Client (Agent):** A Windows-native C++ agent (`ResolutePulse`) running as a Windows Service. It acts as the core orchestrator for data collection, event buffering, and policy enforcement.
*   **Micro-processes (Workers):** The Agent delegates complex or risky operational features to isolated, lightweight child processes (e.g., `rp-softblock.exe`, `rp-antivirus.exe`). They communicate with the main Agent process via Windows Named Pipes (IPC).
*   **Log Pipeline:** A decoupled telemetry ingestion pipeline where the Agent pushes collected logs to Fluent Bit, which forwards them to Data Prepper for processing.
*   **Dashboard:** A React-based Single Page Application providing a graphical interface to interact with the Manager's REST API.

## 2. Data Flow
1.  **Agent Registration:** Upon startup, the Agent initiates a registration flow with the Manager. It generates a CSR and sends it via mTLS. The Manager's CA signs it, records the agent in PostgreSQL, and returns the certificate.
2.  **Command and Control (C2):** The Agent maintains an mTLS connection to the Manager (port 1514). The Manager uses this channel to push configurations, policies, and remote commands to the Agent.
3.  **Telemetry and Logging:**
    *   The Agent collects OS information, File Integrity Monitoring (FIM) events, and Windows Event Logs.
    *   Events are buffered locally using an embedded SQLite database (`EventBuffer`) to prevent data loss during network interruptions.
    *   Buffered events are dispatched via TCP to a local/remote Fluent Bit listener (port 5170).
    *   Fluent Bit normalizes the data and forwards it via HTTP to Data Prepper (port 2021).
4.  **Worker IPC:** The main Agent sends task requests and receives results/status updates from the dedicated worker micro-processes via a bidirectional Named Pipe channel.
5.  **Dashboard API:** The web dashboard interacts with the Manager via REST API (port 8080) to visualize agent status, update policies, and manage licenses.

## 3. Core Abstractions
*   **Event Pipeline (`EventCollector`, `EventBuffer`, `EventQueue`, `BatchSender`):** A resilient, asynchronous pipeline abstraction handling log ingestion. The buffer provides disk-backed persistence (SQLite) for reliability.
*   **WorkerManager & PipeChannel:** Abstractions for spawning, monitoring, and communicating with subprocess workers. `PipeChannel` encapsulates the low-level Windows Named Pipe APIs.
*   **PolicyManager & ModuleController:** Responsible for parsing policy payloads sent from the Manager and routing instructions to the appropriate modules or workers.
*   **FimMonitor & BaselineScanner:** Abstractions for File Integrity Monitoring, utilizing Windows USN Journals and hashing to detect system changes.
*   **Manager components (`CertificateAuthority`, `PostgresClient`, `AgentRegistry`):** Abstractions within the server to handle cryptographic signing, database interactions, and state tracking.

## 4. Entry Points
*   **Agent Entry Point:** `src/main.cpp` - Initializes the `Agent` class and registers the application as a Windows Service or runs it in console mode.
*   **Manager Entry Point:** `src/manager/main.cpp` - Initializes the PostgreSQL connection, CA, mTLS server, and REST API.
*   **Worker Entry Points:** `src/workers/*Worker.cpp` - Individual lightweight `main()` routines for the child processes (e.g., `PatchWorker.cpp`, `WebBlockerWorker.cpp`).
*   **Dashboard Entry Point:** `manager/dashboard/src/main.tsx` - The main rendering entry point for the React frontend application.
