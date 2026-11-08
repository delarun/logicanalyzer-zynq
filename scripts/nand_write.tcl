# =====================================================================
# Запись BOOT.BIN в NAND через JTAG (XVC) с помощью sw/nandwrite.
# program_flash из Vitis 2022.2 с XVC-кабелем падает, поэтому так.
#
# Запуск: xsct scripts/nand_write.tcl <xvc_url> <ps7_init.tcl> <nandwrite.elf> <BOOT.BIN>
# Перемычки загрузки — JTAG (JP1..JP4 сняты).
# =====================================================================

lassign $argv xvc_url ps7init elf boot

set MB     0x01F00000
set IMAGE  0x02000000
set len    [file size $boot]

puts "Подключение к $xvc_url ..."
connect -xvc-url $xvc_url
after 500

targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000
source $ps7init
ps7_init
ps7_post_config
# Тайминги NAND задаёт ps7_init (hw/common/ps7_s9.tcl, set_cycles = 0x00449144)

targets -set -nocase -filter {name =~ "*A9*#0"}
rst -processor
dow $elf
puts "Загрузка образа $boot ($len байт) в DDR ..."
set t0 [clock milliseconds]
dow -data $boot $IMAGE
puts "  [expr {([clock milliseconds] - $t0) / 1000.0}] с"
mwr [expr {$MB + 0}] $len
mwr [expr {$MB + 4}] 0
mwr [expr {$MB + 8}] 0
foreach i {0 1 2 3 4 5 6 7 8} { mwr [expr {$MB + 0x10 + 4 * $i}] 0 }
con

set status 0
for {set i 0} {$i < 600} {incr i} {
    after 1000
    stop
    set status [mrd -value [expr {$MB + 4}]]
    set done   [mrd -value [expr {$MB + 8}]]
    if {$status == 2 || ($status & 0xF000) == 0xE000} break
    puts "  записано [expr {$done / 1024}] / [expr {$len / 1024}] КБ"
    con
}
set diag {}
foreach i {0 1 2 3 4 5 6 7 8} { lappend diag [format 0x%X [mrd -value [expr {$MB + 0x10 + 4 * $i}]]] }
disconnect
puts "Диагностика NAND: rc=[lindex $diag 0] страница=[lindex $diag 1] блок=[lindex $diag 2] блоков=[lindex $diag 3] ширина=[lindex $diag 4] ecc=[lindex $diag 5] id=[lindex $diag 6] id2=[lindex $diag 7] id@20=[lindex $diag 8]"

if {$status == 2} {
    puts "OK: BOOT.BIN записан в NAND. Перемычки — NAND (JP2), затем перезапусти питание."
} else {
    error [format "Ошибка записи NAND: статус 0x%X (подробности в UART)" $status]
}
