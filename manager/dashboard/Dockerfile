# ── Stage 1: Build ──────────────────────────────────────────────────────────
FROM node:20-alpine AS builder

WORKDIR /app

# Copy package files and install dependencies
COPY package*.json ./
RUN npm install

# Copy source and build
COPY . .
RUN npm run build

# ── Stage 2: Runtime ────────────────────────────────────────────────────────
FROM nginx:alpine AS runtime

# Copy build artifacts to nginx public directory
COPY --from=builder /app/dist /usr/share/nginx/html

# Custom nginx config: uses Docker DNS resolver so proxy_pass
# re-resolves the "manager" hostname on every request.
# This prevents 502s when Watchtower restarts the manager container.
RUN echo 'server { \
    listen 80; \
    location / { \
        root /usr/share/nginx/html; \
        index index.html index.htm; \
        try_files $uri $uri/ /index.html; \
    } \
    location /api/ { \
        resolver 127.0.0.11 valid=10s ipv6=off; \
        set $upstream http://manager:8080; \
        proxy_pass $upstream; \
        proxy_set_header Host $host; \
        proxy_set_header X-Real-IP $remote_addr; \
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for; \
        proxy_connect_timeout 5s; \
        proxy_read_timeout 30s; \
    } \
}' > /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
