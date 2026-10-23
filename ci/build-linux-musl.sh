#!/bin/sh
# Build a fully static (musl) aria2c inside the aria2-musl-deps container.
# Usage: bash /src/ci/build-linux-musl.sh /out
set -eu

out="$1"
cp -a /src /tmp/aria2-build
cd /tmp/aria2-build

missing=
for lib in libssl libcrypto libnghttp2 libcares libssh2 libxml2 libexpat \
           libsqlite3 libz libgmp liblzma; do
  # Alpine keeps some early-boot libs (e.g. libz) in /lib, the rest in /usr/lib.
  if ! test -e "/usr/lib/$lib.a" && ! test -e "/lib/$lib.a"; then
    echo "missing static library: $lib.a"
    missing=1
  fi
done
if [ -n "$missing" ]; then
  echo "install the matching Alpine -static packages"
  exit 1
fi

autoreconf -i
./configure --without-gnutls --with-openssl --with-libnghttp2 --disable-nls \
  LDFLAGS='-static' LIBS='-llzma -lz -lm'
make -j"$(nproc)" LDFLAGS='-all-static'

strip src/aria2c
file src/aria2c
file src/aria2c | grep -q 'statically linked'
src/aria2c -v
cp src/aria2c "$out/"
