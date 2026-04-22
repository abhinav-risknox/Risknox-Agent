-- V5: Agent Status Reports table
-- Stores structured status reports pushed back from agents (patch scans, install results, etc.)

CREATE TABLE IF NOT EXISTS agent_status_reports (
    id            SERIAL PRIMARY KEY,
    agent_id      TEXT NOT NULL REFERENCES agents(agent_id),
    report_type   TEXT NOT NULL,           -- 'patch_scan', 'patch_install', 'web_blocking_status', etc.
    report_data   JSONB NOT NULL,          -- full structured data
    created_at    TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_status_reports_agent ON agent_status_reports(agent_id, created_at DESC);
CREATE INDEX idx_status_reports_type ON agent_status_reports(report_type);
