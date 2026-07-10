FROM python:3.9-slim

# Install system dependencies (curl for health checks, no sigma-cli needed)
RUN apt-get update && apt-get install -y curl bash && \
    rm -rf /var/lib/apt/lists/*

# Copy provisioner files
COPY scripts/provision.sh /etc/sigma/provision.sh
COPY scripts/provision.py /etc/sigma/provision.py
COPY scripts/template.json /etc/sigma/template.json


# Ensure scripts are executable
RUN chmod +x /etc/sigma/provision.sh

CMD ["/etc/sigma/provision.sh"]
