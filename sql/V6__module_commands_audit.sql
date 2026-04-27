-- V6: Extend agent_commands with full audit trail and add module_commands table
-- Tracks every MODULE_COMMAND sent from Manager to Agent, including ACK outcome.

-- 1. Extend existing agent_commands with acknowledgement columns
ALTER TABLE agent_commands
    ADD COLUMN IF NOT EXISTS command_id    TEXT,          -- UUID, for correlation with MODULE_COMMAND_RESULT
    ADD COLUMN IF NOT EXISTS command_class TEXT NOT NULL DEFAULT 'policy',  -- 'policy' | 'module'
    ADD COLUMN IF NOT EXISTS ack_status   TEXT,          -- 'success' | 'failed' | 'unsupported'
    ADD COLUMN IF NOT EXISTS ack_message  TEXT,          -- output string from agent
    ADD COLUMN IF NOT EXISTS ack_at       TIMESTAMPTZ;   -- when ACK was received

-- 2. Dedicated module_commands view for the dashboard (filters by class)
CREATE OR REPLACE VIEW module_command_log AS
    SELECT
        id,
        agent_id,
        command_id,
        policy_type           AS verb,
        policy_data           AS params,
        status                AS dispatch_status,
        ack_status,
        ack_message,
        created_at,
        dispatched_at,
        ack_at
    FROM agent_commands
    WHERE command_class = 'module'
    ORDER BY created_at DESC;

-- 3. Index to allow efficient ACK correlation by command_id
CREATE INDEX IF NOT EXISTS idx_agent_commands_command_id
    ON agent_commands(command_id) WHERE command_id IS NOT NULL;
