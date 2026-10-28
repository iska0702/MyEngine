# ==============================================================================
# Dockerfile for MyEngine Dedicated Server
# Multi-stage lightweight headless UDP server image
# ==============================================================================

# Build Stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy engine sources required for the server
COPY src/ /build/src/
COPY Makefile /build/

# Compile headless server binary
RUN make server

# Runtime Stage (Minimal size)
FROM debian:bookworm-slim AS runner

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/bin/MyEngineServer /app/MyEngineServer

# Default configuration
RUN echo "server_name = MyEngine Docker Server\nport = 7777\nmax_players = 32\ntick_rate = 60.0\nnetwork_rate = 20.0" > /app/server.cfg

# Expose default UDP game port
EXPOSE 7777/udp

# Run as non-root user for security
RUN useradd -u 1000 -m serveruser && chown -R serveruser:serveruser /app
USER serveruser

ENTRYPOINT ["/app/MyEngineServer"]
CMD ["--port", "7777"]
