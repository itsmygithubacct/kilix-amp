#!/bin/bash
# Compile termixer from termixer/vendor. cargo --offline --locked must not
# reach the network. Release LTO stays off so the package build finishes.
set -euo pipefail
cd "$(dirname "$0")/.."
host=$(dpkg-architecture -qDEB_HOST_GNU_TYPE)
build=$(dpkg-architecture -qDEB_BUILD_GNU_TYPE)
case "$host" in
  aarch64-linux-gnu) triple=aarch64-unknown-linux-gnu ;;
  x86_64-linux-gnu) triple=x86_64-unknown-linux-gnu ;;
  *) echo "unsupported host $host" >&2; exit 1 ;;
esac
export CARGO_PROFILE_RELEASE_LTO=false
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS=16
extra=()
cc=gcc
libdir=""
out="termixer/target/release/termixer"
if [ "$host" != "$build" ]; then
  key=$(printf '%s' "$triple" | tr 'a-z-' 'A-Z_')
  cc_key=$(printf '%s' "$triple" | tr '-' '_')
  export "CARGO_TARGET_${key}_LINKER=${host}-gcc"
  export "CC_${cc_key}=${host}-gcc"
  export PKG_CONFIG_ALLOW_CROSS=1
  libdir="/usr/lib/${host}/pkgconfig:/usr/share/pkgconfig"
  export PKG_CONFIG_LIBDIR="$libdir"
  extra=(--target "$triple")
  cc="${host}-gcc"
  out="termixer/target/${triple}/release/termixer"
fi
( cd termixer && cargo build --offline --locked --release "${extra[@]}" )
make all CC="$cc"
mkdir -p debian/.built
cp "$out" debian/.built/termixer
printf 'cc=%s\nlibdir=%s\n' "$cc" "$libdir" > debian/.built/env
