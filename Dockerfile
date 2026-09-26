# Build with callbox/container-build.sh so the sibling OPAL and PTLib
# trees are included and their local build directories are left out.
#
#   ./container-build.sh
#   ./container-run.sh callbox.json ./data
#
# The same binary runs on the host:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   ./build/callbox callbox.json

FROM debian:13-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      libavcodec-dev \
      libavformat-dev \
      libavutil-dev \
      libexpat1-dev \
      libopus-dev \
      libspeex-dev \
      libspeexdsp-dev \
      libsqlite3-dev \
      libsrtp2-dev \
      libssl-dev \
      libswresample-dev \
      libswscale-dev \
      libtheora-dev \
      libvpx-dev \
      libx264-dev \
      pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY opalvoip-ptlib /src/opalvoip-ptlib
COPY opalvoip-opal /src/opalvoip-opal
COPY callbox /src/callbox

ARG CALLBOX_H323=ON
ARG CALLBOX_SIP=ON

# One C++ compile job per 1.5 GiB. An uncapped --parallel uses every CPU
# and the container OOM-kills cc1plus ("Killed signal terminated program").
RUN mem_kb=$(awk '/MemAvailable/ {print $2}' /proc/meminfo) \
    && jobs=$(( ${mem_kb:-0} / 1572864 )) \
    && cpus=$(nproc) \
    && if [ "$jobs" -lt 1 ]; then jobs=1; fi \
    && if [ "$jobs" -gt "$cpus" ]; then jobs=$cpus; fi \
    && echo "Compiling with ${jobs} jobs (${mem_kb} kB available)" \
    && cmake -S /src/opalvoip-ptlib -B /tmp/ptlib \
      -DCMAKE_BUILD_TYPE=Release \
      -DPTLIB_BUILD_SAMPLES=OFF \
    && cmake --build /tmp/ptlib --parallel "$jobs" \
    && cmake --install /tmp/ptlib --prefix /usr/local \
    && cmake -S /src/callbox -B /tmp/callbox \
      -DCMAKE_BUILD_TYPE=Release \
      -DCALLBOX_PTLIB_DIR=/usr/local \
      -DCALLBOX_OPAL_DIR=/src/opalvoip-opal \
      -DCALLBOX_H323=${CALLBOX_H323} \
      -DCALLBOX_SIP=${CALLBOX_SIP} \
      -DOPAL_PLUGINS=OFF \
    && cmake --build /tmp/callbox --parallel "$jobs" \
    && cmake --install /tmp/callbox --prefix /usr/local \
    && mkdir -p /export/bin /export/lib \
    && cp -a /usr/local/bin/callbox /export/bin/callbox \
    && cp -a /usr/local/lib/libopal.so* /usr/local/lib/libpt.so* /export/lib/ \
    && ldd /usr/local/bin/callbox /usr/local/lib/libopal.so /usr/local/lib/libpt.so \
      | awk '/=> \// { print $3 }' \
      | sort -u \
      | grep -Ev '/(libc|libm|libdl|libpthread|librt|libresolv|ld-linux)' \
      | xargs -r cp -a -t /export/lib/

FROM debian:13-slim

RUN useradd --create-home --uid 1000 callbox \
    && mkdir -p /config /var/lib/callbox \
    && chown callbox:callbox /var/lib/callbox

COPY --from=build /export /opt/callbox

ENV LD_LIBRARY_PATH=/opt/callbox/lib
ENV CALLBOX_DATABASE=/var/lib/callbox/callbox.db

VOLUME ["/config", "/var/lib/callbox"]
USER callbox
WORKDIR /var/lib/callbox

ENTRYPOINT ["/opt/callbox/bin/callbox"]
CMD ["/config/callbox.json"]
