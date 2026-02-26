-- =====================================================
-- ResolutePulse Manager — Database Schema
-- Run: psql -U postgres -d risknox -f 001_certificates.sql
-- =====================================================

-- 1. Agents table — stores registered agent info
CREATE TABLE IF NOT EXISTS agents (
    id              SERIAL PRIMARY KEY,
    agent_id        VARCHAR(128) NOT NULL UNIQUE,
    hostname        VARCHAR(256) NOT NULL,
    os_type         VARCHAR(64)  NOT NULL,
    os_version      VARCHAR(128),
    agent_version   VARCHAR(32),
    status          VARCHAR(32)  NOT NULL DEFAULT 'ACTIVE',
    cert_serial     VARCHAR(128),
    registered_at   TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    last_seen_at    TIMESTAMP WITH TIME ZONE,
    ip_address      VARCHAR(45),
    
    CONSTRAINT chk_agent_status CHECK (status IN ('ACTIVE', 'INACTIVE', 'REVOKED', 'PENDING'))
);

CREATE INDEX IF NOT EXISTS idx_agents_agent_id ON agents(agent_id);
CREATE INDEX IF NOT EXISTS idx_agents_status   ON agents(status);

-- 2. Certificates table — tracks all issued certificates
CREATE TABLE IF NOT EXISTS certificates (
    id              SERIAL PRIMARY KEY,
    serial_number   VARCHAR(128) NOT NULL UNIQUE,
    agent_id        VARCHAR(128) NOT NULL REFERENCES agents(agent_id),
    certificate_pem TEXT NOT NULL,
    issued_at       TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    expires_at      TIMESTAMP WITH TIME ZONE NOT NULL,
    revoked         BOOLEAN NOT NULL DEFAULT FALSE,
    revoked_at      TIMESTAMP WITH TIME ZONE,
    revoke_reason   VARCHAR(256)
);

CREATE INDEX IF NOT EXISTS idx_certificates_agent_id ON certificates(agent_id);
CREATE INDEX IF NOT EXISTS idx_certificates_serial   ON certificates(serial_number);
CREATE INDEX IF NOT EXISTS idx_certificates_revoked  ON certificates(revoked);

-- 3. Licenses table — controls certificate validity period
CREATE TABLE IF NOT EXISTS licenses (
    id              SERIAL PRIMARY KEY,
    agent_id        VARCHAR(128) NOT NULL REFERENCES agents(agent_id),
    license_key     VARCHAR(256) NOT NULL UNIQUE,
    license_type    VARCHAR(64)  NOT NULL DEFAULT 'TRIAL',
    valid_from      TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    valid_until     TIMESTAMP WITH TIME ZONE NOT NULL,
    max_agents      INTEGER NOT NULL DEFAULT 1,
    created_at      TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    
    CONSTRAINT chk_license_type CHECK (license_type IN ('TRIAL', 'STANDARD', 'ENTERPRISE'))
);

CREATE INDEX IF NOT EXISTS idx_licenses_agent_id ON licenses(agent_id);

-- 4. Agent commands table — pending commands for agents
CREATE TABLE IF NOT EXISTS agent_commands (
    id              SERIAL PRIMARY KEY,
    command_id      VARCHAR(128) NOT NULL UNIQUE,
    agent_id        VARCHAR(128) NOT NULL REFERENCES agents(agent_id),
    command_type    VARCHAR(64)  NOT NULL,
    payload         TEXT,
    status          VARCHAR(32)  NOT NULL DEFAULT 'PENDING',
    created_at      TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    executed_at     TIMESTAMP WITH TIME ZONE,
    result          TEXT,
    
    CONSTRAINT chk_command_status CHECK (status IN ('PENDING', 'SENT', 'EXECUTED', 'FAILED'))
);

CREATE INDEX IF NOT EXISTS idx_commands_agent_id ON agent_commands(agent_id);
CREATE INDEX IF NOT EXISTS idx_commands_status   ON agent_commands(status);
