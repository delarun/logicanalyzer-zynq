#!/bin/bash
# OpenOCD из git с CMSIS-DAP по TCP (для S9 probe, порт 4441) в отдельный префикс.
#   tools/openocd/build.sh
#   ~/.local/opt/openocd-s9/bin/openocd -f tools/openocd/s9probe-swd.cfg -f target/rp2040.cfg
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src="$root/build/openocd/openocd"
prefix="${PREFIX:-$HOME/.local/opt/openocd-s9}"

[ -d "$src" ] || git clone --depth 1 https://github.com/openocd-org/openocd.git "$src"
cd "$src"
git submodule update --init --depth 1 jimtcl
[ -x configure ] || ./bootstrap nosubmodule
mkdir -p build && cd build
[ -f Makefile ] && [ -f ../jimtcl/configure ] || ../configure --prefix="$prefix" --enable-cmsis-dap-tcp --enable-internal-jimtcl \
    --disable-werror --disable-doxygen-html
make -j"$(nproc)"
make install
echo "OK: $prefix/bin/openocd"
