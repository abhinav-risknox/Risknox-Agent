-- V9: RBAC operators table + initiated_by audit column
-- Adds operator authentication and command attribution for audit compliance.

BEGIN;

-- ── Operators table (RBAC) ───────────────────────────────────────────────────
-- Requires pgcrypto extension for crypt() password hashing
CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS operators (
    id            SERIAL PRIMARY KEY,
    username      VARCHAR(100) NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role          VARCHAR(20)  NOT NULL DEFAULT 'operator',  -- admin | operator | viewer
    display_name  VARCHAR(255),
    email         VARCHAR(255),
    active        BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_login_at TIMESTAMPTZ
);

-- Seed a default admin operator (password: RiskNoX@2024)
-- Users should change this immediately in production.
INSERT INTO operators (username, password_hash, role, display_name)
VALUES (
    'admin',
    crypt('RiskNoX@2024', gen_salt('bf', 10)),
    'admin',
    'System Administrator'
) ON CONFLICT (username) DO NOTHING;

-- ── Add initiated_by column to command tables ────────────────────────────────
-- Tracks which operator triggered each command (audit requirement).

ALTER TABLE module_commands
    ADD COLUMN IF NOT EXISTS initiated_by VARCHAR(100);

ALTER TABLE policy_commands
    ADD COLUMN IF NOT EXISTS initiated_by VARCHAR(100);

-- Index for operator-based audit queries
CREATE INDEX IF NOT EXISTS idx_module_commands_initiated_by
    ON module_commands(initiated_by) WHERE initiated_by IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_policy_commands_initiated_by
    ON policy_commands(initiated_by) WHERE initiated_by IS NOT NULL;

-- ── Updated audit views ──────────────────────────────────────────────────────
DROP VIEW IF EXISTS module_command_log;
CREATE OR REPLACE VIEW module_command_log AS
    SELECT id, agent_id, command_id, verb, params,
           status AS dispatch_status, ack_status, result_payload,
           initiated_by, created_at, dispatched_at, ack_at
    FROM module_commands
    ORDER BY created_at DESC;

COMMIT;
