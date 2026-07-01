# ── Build stage ────────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libcurl4-openssl-dev \
    libssl-dev \
    nlohmann-json3-dev \
    libasio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Submodules are already checked-out in the COPY step (they must be committed
# to the repo with `git submodule update --init`).  We do a best-effort init
# here in case the repo was cloned without --recurse-submodules.
RUN git init && git submodule update --init --recursive 2>/dev/null || true

RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
       -DCROW_ENABLE_SSL=OFF \
       -DCROW_AMALGAMATE=OFF \
    && cmake --build . -- -j$(nproc)

# ── Runtime stage ──────────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/openhab_test_suite_backend .

EXPOSE 8080
ENV PORT=8080
ENTRYPOINT ["./openhab_test_suite_backend"]
