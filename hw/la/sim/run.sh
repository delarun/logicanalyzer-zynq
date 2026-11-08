#!/bin/bash
# Симуляция s9la_core в xsim. Запуск: hw/la/sim/run.sh  (нужен source settings64.sh Vivado)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../../build/sim_la"
mkdir -p "$out" && cd "$out"
xvlog -nolog "$here/../rtl/s9la_core.v" "$here/tb_s9la_core.v"
xelab -nolog -debug off tb_s9la_core -s tb
xsim -nolog tb -R | grep -vE '^(INFO|\*\*\*\*|  \*\*|Time resolution|source|## )'
