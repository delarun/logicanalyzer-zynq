# =====================================================================
# Программная часть базового проекта (Vitis/XSCT 2022.2)
#   - платформа из XSA (+ FSBL создаётся автоматически)
#   - echo:    lwIP Echo Server (DHCP), TCP-порт 7
#   - memtest: тест DDR (проверка настроек памяти)
#
# Запуск: xsct sw/base/create_sw.tcl <path/to/s9_base.xsa> <workspace_dir> [local_repo]
# =====================================================================

set xsa [file normalize [lindex $argv 0]]
set ws  [file normalize [lindex $argv 1]]

file delete -force $ws
file mkdir $ws
setws $ws

# Локальный репозиторий (lwIP с патчем под PHY S9)
if {[llength $argv] > 2} {
    set repo [file normalize [lindex $argv 2]]
    repo -set $repo
    repo -scan
    puts "Репозиторий: $repo"
}

platform create -name s9_base -hw $xsa -proc ps7_cortexa9_0 -os standalone -out $ws
platform active s9_base
set dom [domain active]
puts "Домен: $dom"

# lwIP: версия библиотеки зависит от релиза Vitis
set lwip ""
if {[info exists repo] && ![catch {bsp setlib -name lwip211 -ver 1.9}]} {
    set lwip "lwip211 1.9 (патч PHY S9)"
}
foreach lib {lwip213 lwip212 lwip211} {
    if {$lwip ne ""} break
    if {![catch {bsp setlib -name $lib}]} { set lwip $lib }
}
if {$lwip eq ""} { error "Не найдена библиотека lwIP" }
puts "lwIP: $lwip"
bsp config lwip_dhcp           true
bsp config dhcp_does_arp_check true

platform write
platform generate

app create -name echo    -platform s9_base -domain $dom -template {lwIP Echo Server}
app create -name memtest -platform s9_base -domain $dom -template {Memory Tests}

app build -name echo
app build -name memtest
puts "OK"
