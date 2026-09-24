FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG VCPKG_COMMIT=a1cae005c39be7b18ba319fced856b68d7276271

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake build-essential ca-certificates cmake curl git libtool ninja-build \
    perl pkg-config python3 tar unzip zip \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout --detach ${VCPKG_COMMIT} \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /app
COPY CMakeLists.txt vcpkg.json ./
COPY docker-cache/ /opt/vcpkg/downloads/

# GitHub's archive endpoint occasionally returns 504; codeload serves the exact same tagged archive.
RUN test -s /opt/vcpkg/downloads/drogonframework-drogon-v1.9.13.tar.gz \
    || curl --fail --location --retry 5 --retry-all-errors --show-error \
       https://codeload.github.com/drogonframework/drogon/tar.gz/refs/tags/v1.9.13 \
       --output /opt/vcpkg/downloads/drogonframework-drogon-v1.9.13.tar.gz

RUN test -s /opt/vcpkg/downloads/ninja-linux-1.13.2.zip \
    || curl --fail --location --retry 5 --retry-all-errors --show-error \
       https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip \
       --output /opt/vcpkg/downloads/ninja-linux-1.13.2.zip

# Install the pinned dependency graph before copying application source. This
# keeps normal C++ edits from rebuilding Drogon and the MongoDB driver.
RUN --mount=type=cache,target=/root/.cache/vcpkg/archives \
    /opt/vcpkg/vcpkg install \
      --x-manifest-root=/app \
      --x-install-root=/app/build/vcpkg_installed \
      --triplet=x64-linux

COPY backend ./backend

RUN --mount=type=cache,target=/root/.cache/vcpkg/archives \
    cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build --parallel 2 \
    && ctest --test-dir build --output-on-failure --exclude-regex tiktok_mongo_integration

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl ffmpeg \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /app/build/tiktok_backend /usr/local/bin/tiktok_backend
COPY --from=build /app/build/vcpkg_installed/x64-linux/lib/ /usr/local/lib/
RUN ldconfig && useradd --create-home --uid 10001 appuser && mkdir -p /data/media && chown -R appuser:appuser /data

USER appuser
WORKDIR /home/appuser
ENV MEDIA_ROOT=/data/media BACKEND_LISTEN_ADDRESS=0.0.0.0 BACKEND_PORT=8080
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=5s --start-period=10s --retries=10 \
  CMD curl --fail --silent http://127.0.0.1:8080/api/health >/dev/null || exit 1
ENTRYPOINT ["tiktok_backend"]
