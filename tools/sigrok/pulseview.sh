#!/bin/bash
# PulseView из tools/sigrok/build.sh, сразу подключённый к анализатору S9.
#   tools/sigrok/pulseview.sh [host]      (по умолчанию $S9LA_HOST или 192.168.10.224)
prefix="${PREFIX:-$HOME/.local/opt/sigrok-s9}"
host="${1:-${S9LA_HOST:-192.168.10.224}}"
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Нативный Wayland (Qt6), X11 — запасной вариант. Переопределить: QT_QPA_PLATFORM=xcb
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"
exec "$prefix/bin/pulseview" -d "antminer-s9-la:conn=$host" "${@:2}"
