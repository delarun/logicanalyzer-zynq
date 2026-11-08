# =====================================================================
# Загрузка битстрима и запуск программы на ARM через JTAG (XVC / ESP8266)
#
# Запуск: xsct scripts/jtag_run.tcl <xvc_url> <bit> <ps7_init.tcl> <elf>
#   xvc_url — например TCP:192.168.1.50:2542
# Перемычки загрузки лучше поставить в режим JTAG (JP1..JP4 = 0).
# =====================================================================

lassign $argv xvc_url bit ps7init elf

puts "Подключение к $xvc_url ..."
connect -xvc-url $xvc_url
after 500
puts [targets]

targets -set -nocase -filter {name =~ "APU*"}
# -stop: ядра стоят после сброса — плата не начнёт грузиться с NAND/SD, даже если
# перемычки не в режиме JTAG
rst -system -stop
after 1000

puts "Битстрим: $bit"
fpga -file $bit

targets -set -nocase -filter {name =~ "APU*"}
source $ps7init
ps7_init
ps7_post_config

targets -set -nocase -filter {name =~ "*A9*#0"}
puts "Программа: $elf"
dow $elf
con
puts "Запущено. Вывод смотри в UART (115200)."
disconnect
