# Codebase Tech Stack
**Date Analyzed**: 2026-05-25

## 1. Core Agent (ResolutePulse Agent)
- **Language**: C++17, C11
- **Build System**: CMake (3.20+)
- **Key Libraries/Dependencies**:
  - `OpenSSL` (Cryptography and TLS)
  - `SQLite3` (Local embedded database)
  - `spdlog`, `fmt` (Logging)
  - `nlohmann-json` (JSON parsing)
  - `cpp-httplib` (HTTP client)
  - Windows APIs: `wevtapi`, `advapi32`, `ws2_32`, `bcrypt`, `iphlpapi`, `netapi32`, `ole32`, `oleaut32`

## 2. Manager (ResolutePulse Manager)
- **Language**: C++17
- **Runtime**: Multi-stage Docker (Ubuntu 22.04)
- **Key Libraries**:
  - `OpenSSL`, `spdlog`, `nlohmann-json`, `cpp-httplib`
  - `libpq` (PostgreSQL C++ integration)

## 3. ResolutePulse Dashboard (`manager/dashboard`)
- **Language**: TypeScript
- **Frameworks**: React 19, React Router v7
- **State Management & Fetching**: React Query (@tanstack/react-query), Axios
- **Styling**: Tailwind CSS 4, `clsx`, `tailwind-merge`
- **Charting & Icons**: Recharts, Lucide React
- **Build Tool**: Vite

## 4. Pulse Backend API (`Pulse/backend`)
- **Language**: Node.js (>=18.x)
- **Framework**: Express.js
- **Key Dependencies**:
  - `pg` (PostgreSQL client)
  - `kafkajs` (Kafka integration)
  - `jsonwebtoken`, `bcrypt` (Authentication & Security)
  - `cors`, `dotenv`, `nodemailer`

## 5. Pulse Frontend (`Pulse/frontend`)
- **Language**: JavaScript
- **Frameworks**: React 18, React Router 6, Redux Toolkit
- **Styling**: Bootstrap 5, Reactstrap, Sass
- **Charting**: ApexCharts, Chart.js, ECharts
- **Utilities**: Formik, Yup, i18next, Firebase JS SDK
- **Build Tool**: Create React App (react-scripts)

## 6. Python Backend Services (`backend_server_complete_with_blocking.py`)
- **Language**: Python 3
- **Framework**: Flask, Flask-CORS
- **Key Dependencies**: `psutil`, `schedule`, `win32com` (pywin32)

## 7. Infrastructure & Data Layer (`docker-compose.yml`)
- **Containerization**: Docker & Docker Compose
- **Databases**: PostgreSQL 16 (Alpine)
- **Message Broker**: Confluent Kafka
- **Logging & Search**: OpenSearch, OpenSearch Dashboards, Data Prepper, Fluent-bit
