#!/bin/bash
# ec2-bootstrap.sh
# Run ONCE on the EC2 instance to prepare it for automated deployments.
# Usage: bash ec2-bootstrap.sh <dockerhub-username> <dockerhub-token>

set -e

DOCKERHUB_USER="${1:?Usage: $0 <dockerhub-username> <dockerhub-token>}"
DOCKERHUB_TOKEN="${2:?Usage: $0 <dockerhub-username> <dockerhub-token>}"

echo "=== [1/4] Logging into Docker Hub ==="
echo "$DOCKERHUB_TOKEN" | docker login --username "$DOCKERHUB_USER" --password-stdin

echo "=== [2/4] Updating docker-compose.yml with correct image name ==="
# Replace the placeholder with the real Docker Hub username
sed -i "s|DOCKERHUB_USERNAME|${DOCKERHUB_USER}|g" ~/manager/docker-compose.yml

echo "=== [3/4] Pulling all images ==="
cd ~/manager
docker compose pull

echo "=== [4/4] Starting all services ==="
docker compose up -d

echo ""
echo "✅ Bootstrap complete!"
echo "   Manager image : ${DOCKERHUB_USER}/risknox-manager:latest"
echo "   Watchtower    : polls every 5 minutes for new images"
echo ""
echo "Future deploys are fully automated via GitHub Actions."
echo "You never need to SCP or SSH for deployments again."
