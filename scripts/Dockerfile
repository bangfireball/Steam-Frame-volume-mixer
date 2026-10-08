FROM debian:bookworm-slim
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates pkg-config python3 file \
    g++-aarch64-linux-gnu qemu-user \
    libpulse-dev libcairo2-dev libvulkan-dev fonts-dejavu-core pulseaudio pulseaudio-utils \
    libpulse-dev:arm64 libcairo2-dev:arm64 libvulkan-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m -u 1000 tester
WORKDIR /work
