-- V7: Split agent_commands into policy_commands + module_commands
-- Preserves all historical data. agent_commands is kept and can be dropped
-- manually after verifying the new tables are correct.

BEGIN;

-- ── policy_commands ──────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS policy_commands (
    id            SERIAL PRIMARY KEY,
    agent_id      VARCHAR(255) NOT NULL,
    command_id    TEXT NOT NULL DEFAULT '',
    policy_type   VARCHAR(50)  NOT NULL,   -- 'antivirus' | 'patch_management' | 'web_blocking' | 'software_blocking'
    policy_data   JSONB        NOT NULL,
    status        VARCHAR(20)  NOT NULL DEFAULT 'pending',  -- pending | sent | acked | failed
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    dispatched_at TIMESTAMPTZ,
    ack_status    TEXT,                    -- 'applied' | 'failed'
    ack_message   TEXT,
    ack_at        TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_policy_commands_pending
    ON policy_commands(agent_id, status) WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS idx_policy_commands_command_id
    ON policy_commands(command_id) WHERE command_id <> '';

-- ── module_commands ──────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS module_commands (
    id            SERIAL PRIMARY KEY,
    agent_id      VARCHAR(255) NOT NULL,
    command_id    TEXT NOT NULL DEFAULT '',
    verb          VARCHAR(100) NOT NULL,   -- 'av_version' | 'av_update' | 'diagnostics' | 'agent_restart' | etc.
    params        JSONB        NOT NULL DEFAULT '{}',
    status        VARCHAR(20)  NOT NULL DEFAULT 'pending',  -- pending | sent | acked | failed
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    dispatched_at TIMESTAMPTZ,
    ack_status    TEXT,                    -- 'success' | 'failed' | 'unsupported'
    result_payload TEXT,                   -- full JSON output from ModuleCommandResult
    ack_at        TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_module_commands_pending
    ON module_commands(agent_id, status) WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS idx_module_commands_command_id
    ON module_commands(command_id) WHERE command_id <> '';

-- ── Migrate historical data ──────────────────────────────────────────────────
INSERT INTO policy_commands
    (agent_id, command_id, policy_type, policy_data, status, created_at,
     dispatched_at, ack_status, ack_message, ack_at)
SELECT
    agent_id,
    COALESCE(command_id, ''),
    policy_type,
    policy_data,
    status,
    created_at,
    dispatched_at,
    ack_status,
    ack_message,
    ack_at
FROM agent_commands
WHERE command_class = 'policy' OR command_class IS NULL;

INSERT INTO module_commands
    (agent_id, command_id, verb, params, status, created_at,
     dispatched_at, ack_status, result_payload, ack_at)
SELECT
    agent_id,
    COALESCE(command_id, ''),
    policy_type,   -- policy_type held the verb name for module rows
    policy_data,   -- policy_data held the params JSON for module rows
    status,
    created_at,
    dispatched_at,
    ack_status,
    ack_message,   -- ack_message becomes result_payload
    ack_at
FROM agent_commands
WHERE command_class = 'module';

-- ── Improve agent_status_reports retention/query indexes ─────────────────────
CREATE INDEX IF NOT EXISTS idx_status_reports_retention
    ON agent_status_reports(created_at ASC);

CREATE INDEX IF NOT EXISTS idx_status_reports_agent_type
    ON agent_status_reports(agent_id, report_type, created_at DESC);

-- ── Recreate module_command_log view on the new table ────────────────────────
DROP VIEW IF EXISTS module_command_log;
CREATE OR REPLACE VIEW module_command_log AS
    SELECT id, agent_id, command_id, verb, params,
           status AS dispatch_status, ack_status, result_payload,
           created_at, dispatched_at, ack_at
    FROM module_commands
    ORDER BY created_at DESC;

-- NOTE: agent_commands is intentionally NOT dropped here.
-- After verifying that policy_commands + module_commands contain all expected
-- rows, run: DROP TABLE agent_commands;
COMMIT;
