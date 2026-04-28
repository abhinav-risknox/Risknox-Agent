-- V8: Schema fixes for command tables
-- Enforces commandId uniqueness, adds TTL expiry, and adds retention tooling.

BEGIN;

-- ── policy_commands: enforce UNIQUE on command_id ────────────────────────────
-- Drop old plain index (non-unique), replace with a unique partial index.
-- Partial WHERE keeps empty-string default rows out of the uniqueness check.
DROP INDEX IF EXISTS idx_policy_commands_command_id;

CREATE UNIQUE INDEX IF NOT EXISTS idx_policy_commands_command_id
    ON policy_commands(command_id)
    WHERE command_id <> '';

-- Add expires_at column: populated by trigger on INSERT.
-- (GENERATED ALWAYS AS is not supported for timestamptz arithmetic in PG.)
ALTER TABLE policy_commands
    ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;

-- Backfill existing rows
UPDATE policy_commands SET expires_at = created_at + INTERVAL '30 days'
WHERE expires_at IS NULL;

-- Trigger function: auto-set expires_at = created_at + 30 days on INSERT
CREATE OR REPLACE FUNCTION set_policy_command_expiry()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.expires_at := NEW.created_at + INTERVAL '30 days';
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_policy_command_expiry ON policy_commands;
CREATE TRIGGER trg_policy_command_expiry
    BEFORE INSERT ON policy_commands
    FOR EACH ROW EXECUTE FUNCTION set_policy_command_expiry();

-- ── module_commands: enforce UNIQUE on command_id ────────────────────────────
DROP INDEX IF EXISTS idx_module_commands_command_id;

CREATE UNIQUE INDEX IF NOT EXISTS idx_module_commands_command_id
    ON module_commands(command_id)
    WHERE command_id <> '';

-- Add expires_at column
ALTER TABLE module_commands
    ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;

UPDATE module_commands SET expires_at = created_at + INTERVAL '30 days'
WHERE expires_at IS NULL;

CREATE OR REPLACE FUNCTION set_module_command_expiry()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.expires_at := NEW.created_at + INTERVAL '30 days';
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_module_command_expiry ON module_commands;
CREATE TRIGGER trg_module_command_expiry
    BEFORE INSERT ON module_commands
    FOR EACH ROW EXECUTE FUNCTION set_module_command_expiry();

-- ── Cleanup view: stale pending commands ──────────────────────────────────────
CREATE OR REPLACE VIEW stale_pending_commands AS
    SELECT 'policy' AS command_class, id, agent_id, command_id,
           policy_type AS verb_or_type, status, created_at, expires_at
    FROM policy_commands
    WHERE status = 'pending' AND expires_at < NOW()
    UNION ALL
    SELECT 'module', id, agent_id, command_id,
           verb, status, created_at, expires_at
    FROM module_commands
    WHERE status = 'pending' AND expires_at < NOW()
    ORDER BY created_at ASC;

-- ── Retention index on agent_status_reports ──────────────────────────────────
CREATE INDEX IF NOT EXISTS idx_status_reports_retention
    ON agent_status_reports(created_at ASC);

COMMIT;

