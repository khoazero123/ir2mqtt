# IR2MQTT add-on — local build FROM SOURCE (dev)
# Adapted from upstream ir2mqtt/Dockerfile.ha-app:
#   * no --platform=$BUILDPLATFORM (HAOS builds with plain docker build)
#   * base image comes from build.yaml (BUILD_FROM) so it uses the HA base image
#   * source is COPY'd from this add-on folder (edit here -> rebuild in HA)
#   * extra stage builds libkksdk_host.so (Kookong native host library) so the
#     Kookong offline IR database provider works on the HA base image (Alpine/musl)

ARG BUILD_FROM

# ---------------------------------------------------------------------------
# Stage 1: frontend
# ---------------------------------------------------------------------------
FROM node:22-alpine AS builder
WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

# ---------------------------------------------------------------------------
# Stage 2: Kookong native host library (from vendored reverse-engineered source)
#
# NOTE: the OEM libkksdk.so from the APK cannot be used here — it is an
# Android/bionic JNI library that does not load on Linux (missing bionic
# sonames + symbol version 'LIBC' + JNI-only exports).  This builds the portable
# host package (C ABI, no JNI) which does run on aarch64/amd64/armv7 Linux.
# ---------------------------------------------------------------------------
FROM ${BUILD_FROM} AS kksdk-builder
RUN apk add --no-cache build-base cmake
COPY kksdk-src/CMakeLists.txt /kksdk-src/
COPY kksdk-src/src /kksdk-src/src
RUN cmake -S /kksdk-src -B /kksdk-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DKKSDK_BUILD_HOST_TESTS=OFF \
    && cmake --build /kksdk-build -j"$(nproc)" \
    && mkdir -p /kksdk-out \
    && cp /kksdk-build/libkksdk_host.so /kksdk-build/libluajava_host.so /kksdk-out/ \
    && ls -l /kksdk-out

# ---------------------------------------------------------------------------
# Stage 3: runtime
# ---------------------------------------------------------------------------
FROM ${BUILD_FROM}
ARG APP_VERSION=dev
ENV APP_VERSION=${APP_VERSION}
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

# libstdc++ is listed explicitly so it survives the `apk del g++` below:
# libkksdk_host.so is a C++ shared library and needs it at runtime.
RUN apk add --no-cache python3 py3-pip bash gcc g++ musl-dev python3-dev libstdc++

WORKDIR /app
COPY --from=builder /app/dist ./dist

COPY requirements.txt .
RUN pip3 install --no-cache-dir -r requirements.txt --break-system-packages \
    && apk del gcc g++ musl-dev python3-dev

# Kookong native host library + offline database
COPY --from=kksdk-builder /kksdk-out/libkksdk_host.so /usr/lib/libkksdk_host.so
COPY --from=kksdk-builder /kksdk-out/libluajava_host.so /usr/lib/libluajava_host.so
RUN mkdir -p /usr/local/share/kookong
COPY kksdk-src/data/kkoffline.db /usr/local/share/kookong/kkoffline.db

COPY migrations /app/migrations
COPY alembic.ini .
COPY backend /app/backend
COPY run.sh /app/run.sh
RUN chmod a+x /app/run.sh

CMD [ "/app/run.sh" ]
