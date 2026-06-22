FROM python:3.9-slim

# Install system dependencies and sigma tools
RUN apt-get update && apt-get install -y curl bash && \
    pip install sigma-cli pysigma-backend-opensearch && \
    sigma plugin install -f opensearch && \
    rm -rf /var/lib/apt/lists/*

# Copy pipeline config and startup script
COPY sigma/pipeline.yml /etc/sigma/pipeline.yml
COPY scripts/load_sigma.sh /entrypoint.d/load_sigma.sh

# Ensure scripts are executable
RUN chmod +x /entrypoint.d/load_sigma.sh

CMD ["/entrypoint.d/load_sigma.sh"]
