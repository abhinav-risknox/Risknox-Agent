# =============================================================================
# ResolutePulse Manager - Multi-Stage Docker Build
# =============================================================================

# ── Stage 1: Build ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    libssl-dev \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only what's needed for the Manager build
COPY manager/CMakeLists.txt ./CMakeLists.txt
COPY src/manager/          ./src/manager/
COPY src/utils/            ./src/utils/
COPY src/common/           ./src/common/
COPY thirdparty/spdlog/    ./thirdparty/spdlog/
COPY thirdparty/nlohmann/  ./thirdparty/nlohmann/
COPY thirdparty/httplib/   ./thirdparty/httplib/

# Build
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel $(nproc)

# ── Stage 2: Runtime ────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libpq5 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the built binary
COPY --from=builder /app/build/ResolutePulseManager .

# Copy default configuration file
COPY docker/manager_config.json /app/manager_config.json

EXPOSE 1514 1515 1535 1536 8080

ENTRYPOINT ["./ResolutePulseManager", "--config", "/app/manager_config.json"]
