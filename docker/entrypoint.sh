#!/bin/bash
set -e

echo "=== Starting Risknox All-In-One Container ==="

# 1. Initialize PostgreSQL if empty
if [ ! -d "/var/lib/postgresql/16/main" ] || [ -z "$(ls -A /var/lib/postgresql/16/main)" ]; then
    echo "=== Initializing PostgreSQL cluster ==="
    mkdir -p /var/lib/postgresql/16/main
    chown -R postgres:postgres /var/lib/postgresql
    su - postgres -c "/usr/lib/postgresql/16/bin/initdb -D /var/lib/postgresql/16/main"
fi

# Start postgres temporarily to configure DB and schema
chown -R postgres:postgres /var/lib/postgresql
su - postgres -c "/usr/lib/postgresql/16/bin/pg_ctl -D /var/lib/postgresql/16/main -l /var/log/postgresql/init.log start"

# Wait for postgres readiness
until su - postgres -c "pg_isready -h 127.0.0.1 -p 5432"; do
    echo "Waiting for PostgreSQL..."
    sleep 1
done

# Create user & database if not exists
su - postgres -c "psql -tc \"SELECT 1 FROM pg_user WHERE usename = 'postgres'\" | grep -q 1 || psql -c \"CREATE USER postgres WITH PASSWORD 'postgres' SUPERUSER;\""
su - postgres -c "psql -tc \"SELECT 1 FROM pg_database WHERE datname = 'risknox'\" | grep -q 1 || psql -c \"CREATE DATABASE risknox OWNER postgres;\""

# Apply initial schema if table doesn't exist
if [ -f "/app/db/schema.sql" ]; then
    echo "=== Applying schema.sql ==="
    su - postgres -c "psql -d risknox -f /app/db/schema.sql" || true
fi

# Stop temporary postgres so supervisord can manage it cleanly
su - postgres -c "/usr/lib/postgresql/16/bin/pg_ctl -D /var/lib/postgresql/16/main stop"

# 2. Background task: Wait for OpenSearch and run Sigma provisioner
(
    echo "=== [Background] Waiting for OpenSearch on 127.0.0.1:9200..."
    until curl -sf "http://127.0.0.1:9200/_cluster/health" | grep -q '"status":"green"\|"status":"yellow"'; do
        sleep 5
    done
    echo "=== OpenSearch is healthy! Running Sigma Provisioner... ==="
    OPENSEARCH_URL="http://127.0.0.1:9200" INDEX_NAME="rp-events" DETECTOR_NAME="Windows-Threat-Detector" /etc/sigma/provision.sh || true
) &

# 3. Launch supervisord in foreground
echo "=== Starting Supervisord Daemon ==="
exec /usr/bin/supervisord -c /etc/supervisor/conf.d/supervisord.conf
