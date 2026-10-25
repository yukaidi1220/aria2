# Runtime image for this fork's aria2: a fully static (musl) aria2c plus a
# minimal entrypoint that renders aria2.conf from environment variables.
#
# The dependency set and build invocation mirror Dockerfile.musl-ci and
# ci/build-linux-musl.sh (the proven CI recipe). The only adaptation is the
# OpenSSL target, which is derived from `apk --print-arch` so the same
# Dockerfile builds on every platform (x86_64 -> linux-x86_64,
# aarch64 -> linux-aarch64).

ARG ALPINE_VERSION=3.20

# Dependencies stage: build tools, static libs, and a source-built OpenSSL
# (no-shared, no-module so the legacy provider is builtin; the split
# -static packages' libcrypto does not activate it inside a static binary).
FROM alpine:${ALPINE_VERSION} AS deps
RUN apk add --no-cache \
      build-base autoconf automake libtool pkgconfig gettext gettext-dev \
      linux-headers file bash curl perl \
      nghttp2-dev nghttp2-static c-ares-dev c-ares-static \
      libssh2-dev libssh2-static expat-dev expat-static sqlite-dev sqlite-static \
      zlib-dev zlib-static gmp-dev libxml2-dev libxml2-static xz-dev xz-static
RUN curl --fail -L --retry 5 --retry-all-errors -o /tmp/openssl.tar.gz \
      https://github.com/openssl/openssl/releases/download/openssl-3.3.7/openssl-3.3.7.tar.gz && \
    tar xf /tmp/openssl.tar.gz -C /tmp && \
    cd /tmp/openssl-3.3.7 && \
    ./Configure linux-$(apk --print-arch) no-shared no-module no-tests --libdir=lib \
      --prefix=/usr/local/openssl --openssldir=/etc/ssl && \
    make -j"$(nproc)" && \
    make install_sw

# Build stage: compile and verify the fully static aria2c.
FROM deps AS build
COPY . /src
RUN cp -a /src /tmp/aria2-build && cd /tmp/aria2-build && \
    autoreconf -i && \
    PKG_CONFIG_PATH=/usr/local/openssl/lib/pkgconfig \
    ./configure --without-gnutls --with-openssl --with-libnghttp2 --disable-nls \
    LDFLAGS='-static' LIBS='-llzma -lz -lm' && \
    make -j"$(nproc)" LDFLAGS='-all-static' && \
    strip src/aria2c && \
    file src/aria2c | grep -q 'statically linked' && \
    src/aria2c -v && \
    mkdir -p /out && cp src/aria2c /out/

# Runtime stage: static binary, CA bundle, generated config.
FROM alpine:${ALPINE_VERSION}
RUN apk add --no-cache ca-certificates
COPY --from=build /out/aria2c /usr/local/bin/aria2c
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh /usr/local/bin/aria2c && \
    mkdir -p /downloads /config
ENV RPC_PORT=6800 \
    RPC_SECRET= \
    DOWNLOAD_DIR=/downloads \
    CONF_DIR=/config
VOLUME ["/downloads", "/config"]
EXPOSE 6800
ENTRYPOINT ["docker-entrypoint.sh"]
