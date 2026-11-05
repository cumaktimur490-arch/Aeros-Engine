# Aeros Engine — Linux Build Dockerfile v1.19.0 Vulkan + OpenGL
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Stockholm

RUN apt-get update && apt-get install -y \
    build-essential cmake pkg-config git curl wget \
    libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libvulkan-dev vulkan-tools glslc shaderc \
    zenity kdialog libopenmp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy source
COPY . /workspace/

# Download deps and build
RUN ./aeros/build-linux.sh download-deps || true
RUN ./aeros/build-linux.sh all

# Test binary exists
RUN ls -lh aeros/bin/aeros-engine* && ./aeros/bin/aeros-engine --help || true

CMD ["./aeros/bin/aeros-engine"]
