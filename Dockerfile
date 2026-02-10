# Stage 1: Build
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    cmake \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DPORTABLE=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    && cmake --build build --target strikemq

# Stage 2: Runtime
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libatomic1 \
    && rm -rf /var/lib/apt/lists/*

LABEL org.opencontainers.image.title="StrikeMQ" \
      org.opencontainers.image.description="Sub-millisecond Kafka-compatible message broker for development and testing" \
      org.opencontainers.image.version="0.1.4" \
      org.opencontainers.image.url="https://github.com/awneesht/Strike-mq" \
      org.opencontainers.image.source="https://github.com/awneesht/Strike-mq" \
      org.opencontainers.image.licenses="MIT"

COPY --from=builder /src/build/strikemq /usr/local/bin/strikemq

EXPOSE 9092 8080

CMD ["strikemq"]
