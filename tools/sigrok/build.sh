#!/bin/bash
# Сборка libsigrokdecode + libsigrok (с драйвером antminer-s9-la) + PulseView + sigrok-cli
# в отдельный префикс, не трогая системные пакеты.
#
#   tools/sigrok/build.sh            # всё
#   tools/sigrok/build.sh libsigrok  # только libsigrok (после правок драйвера)
#
# Запуск PulseView: ~/.local/opt/sigrok-s9/bin/pulseview  (или tools/sigrok/pulseview.sh)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src="${SIGROK_SRC:-$root/build/sigrok/src}"
bld="${SIGROK_BUILD:-$root/build/sigrok/build}"
prefix="${PREFIX:-$HOME/.local/opt/sigrok-s9}"
jobs="${JOBS:-$(nproc)}"

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$src" "$bld"

fetch() {
    [ -d "$src/$1" ] || git clone https://github.com/sigrokproject/$1.git "$src/$1"
}

autotools_build() {   # name [configure args...]
    local name=$1; shift
    (cd "$src/$name" && [ -x configure ] || ./autogen.sh)
    mkdir -p "$bld/$name"
    cd "$bld/$name"
    [ -f Makefile ] || "$src/$name/configure" --prefix="$prefix" "$@"
    make -j"$jobs"
    make install
}

inject_driver() {
    local d="$src/libsigrok"
    rm -rf "$d/src/hardware/antminer-s9-la"
    cp -r "$here/driver" "$d/src/hardware/antminer-s9-la"
    if ! grep -q antminer-s9-la "$d/configure.ac"; then
        sed -i 's|^SR_DRIVER(\[BeagleLogic\], \[beaglelogic\].*|&\nSR_DRIVER([Antminer S9 LA], [antminer-s9-la])|' "$d/configure.ac"
        sed -i 's|^if HW_ASIX_SIGMA$|if HW_ANTMINER_S9_LA\nsrc_libdrivers_la_SOURCES += \\\n\tsrc/hardware/antminer-s9-la/protocol.h \\\n\tsrc/hardware/antminer-s9-la/protocol.c \\\n\tsrc/hardware/antminer-s9-la/api.c\nendif\n&|' "$d/Makefile.am"
        # configure.ac поменялся — перегенерировать
        (cd "$d" && ./autogen.sh)
        rm -f "$bld/libsigrok/Makefile"
    fi
}

build_libsigrokdecode() {
    fetch libsigrokdecode
    autotools_build libsigrokdecode
}

build_libsigrok() {
    fetch libsigrok
    inject_driver
    # Java/Ruby/Python-обвязки не нужны, C++ нужна PulseView
    autotools_build libsigrok --disable-java --disable-ruby --disable-python --enable-cxx
}

build_cli() {
    fetch sigrok-cli
    autotools_build sigrok-cli
}

# Qt6 по умолчанию: под Wayland (niri) Qt5-версия заметно лагает при перерисовке.
# PV_QT=5 — собрать на Qt5.
build_pulseview() {
    local qt="${PV_QT:-6}" qtopt=()
    [ "$qt" = 6 ] && qtopt=(-DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
    fetch pulseview
    mkdir -p "$bld/pulseview-qt$qt"
    cd "$bld/pulseview-qt$qt"
    cmake "$src/pulseview" "${qtopt[@]}" -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_INSTALL_RPATH="$prefix/lib" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDISABLE_WERROR=TRUE -DENABLE_TESTS=FALSE
    make -j"$jobs"
    make install
}

what="${1:-all}"
case "$what" in
    all)              build_libsigrokdecode; build_libsigrok; build_cli; build_pulseview ;;
    libsigrokdecode)  build_libsigrokdecode ;;
    libsigrok)        build_libsigrok ;;
    cli|sigrok-cli)   build_cli ;;
    pulseview)        build_pulseview ;;
    *) echo "неизвестная цель: $what"; exit 1 ;;
esac
echo "OK: $what → $prefix"
