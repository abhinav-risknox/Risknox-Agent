-- ResolutePulse: Agent command queue
-- Persists commands for offline delivery and provides an audit trail.
-- The primary delivery path is the Manager's command ingest socket (port 1515).
-- This table is checked on agent reconnect to drain any commands missed while offline.

CREATE TABLE IF NOT EXISTS agent_commands (
    id            SERIAL PRIMARY KEY,
    agent_id      VARCHAR(255) NOT NULL,
    policy_type   VARCHAR(50)  NOT NULL,  -- 'software_blocking' | 'web_blocking' | 'patch_management' | 'antivirus'
    policy_data   JSONB        NOT NULL,
    status        VARCHAR(20)  NOT NULL DEFAULT 'pending',  -- pending | sent | failed | offline
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    dispatched_at TIMESTAMPTZ,
    error_message TEXT
);

-- Fast lookup for pending commands per agent (used on reconnect drain)
CREATE INDEX IF NOT EXISTS idx_agent_commands_pending_agent
    ON agent_commands(agent_id, status) WHERE status = 'pending';
