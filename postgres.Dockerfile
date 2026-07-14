FROM postgres:16-alpine

# Copy schema initialization script to the initialization directory
COPY db/migrations/schema.sql /docker-entrypoint-initdb.d/001_schema.sql
