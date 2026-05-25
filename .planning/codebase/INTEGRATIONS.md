# External Integrations
**Date Analyzed**: 2026-05-25

## 1. Authentication & Identity
- **Firebase Auth**: Integrated within the Pulse Frontend UI (`Pulse/frontend`) to handle user sign-ins, utilizing both Google and Facebook OAuth providers.
- **JWT (JSON Web Tokens)**: Utilized by the Node.js Pulse Backend for secure API endpoint protection and session management.

## 2. Databases & Storage
- **PostgreSQL**: Serves as the primary relational database for both the ResolutePulse Manager (`risknox` database) and the Node.js Pulse Backend.
- **SQLite3**: Employed as an embedded local database for the C++ ResolutePulse Agent for FIM (File Integrity Monitoring) baselines and event buffering.
- **Firebase Firestore**: Cloud NoSQL database accessed directly from the Pulse Frontend for user profiles and specific data storage.

## 3. Data Streaming & Log Pipelines
- **Confluent Kafka**: Event streaming platform utilized by the Pulse Backend (`kafkajs` integration) for high-throughput messaging.
- **OpenSearch**: Core search and analytics engine for storing and querying collected events.
- **OpenSearch Dashboards**: Visual interface integrated for monitoring security logs.
- **Fluent-bit**: Lightweight log processor and forwarder running in a container, shipping local events to Data Prepper/OpenSearch.
- **Data Prepper**: Observability data collector handling transformations and pipeline routing before indexing data in OpenSearch.

## 4. Communications & Notifications
- **Nodemailer**: Node.js module utilized in the Pulse Backend to dispatch automated emails via SMTP protocols.

## 5. OS-Level Integrations (Windows)
- **Windows APIs (COM / WMI / Registry)**: The Python backend and C++ Agent interact closely with native OS capabilities (`win32com.client`, `wevtapi`, `advapi32`) to manage patches, perform system scans, block applications, and monitor event logs.
