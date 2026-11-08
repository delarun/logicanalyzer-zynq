# =====================================================================
# Прошивка логического анализатора (Vitis/XSCT 2022.2)
#   - платформа из XSA (+ FSBL)
#   - BSP: lwIP с патчем под PHY S9, настроен на большой поток TX
#   - приложение s9la: шаблон lwIP Echo Server (только platform_*.c) + sw/la/src
#   - nandwrite: запись BOOT.BIN в NAND через JTAG (sw/nandwrite/src)
#
# Запуск: xsct sw/la/create_sw.tcl <s9_la.xsa> <workspace> <local_repo> <src_dir>
# =====================================================================

lassign $argv xsa ws repo src
set xsa  [file normalize $xsa]
set ws   [file normalize $ws]
set repo [file normalize $repo]
set src  [file normalize $src]

file delete -force $ws
file mkdir $ws
setws $ws

repo -set $repo
repo -scan

platform create -name s9_la -hw $xsa -proc ps7_cortexa9_0 -os standalone -out $ws
platform active s9_la
set dom [domain active]

bsp setlib -name lwip211 -ver 1.9
foreach {k v} {
    lwip_dhcp             true
    dhcp_does_arp_check   true
    mem_size              524288
    memp_n_pbuf           1024
    memp_n_tcp_seg        1024
    pbuf_pool_size        1024
    tcp_snd_buf           65535
    tcp_wnd               16384
    memp_n_tcp_pcb_listen 16
    n_tx_descriptors      512
    n_rx_descriptors      128
} {
    bsp config $k $v
}

platform write
platform generate

app create -name s9la -platform s9_la -domain $dom -template {lwIP Echo Server}
set appsrc "$ws/s9la/src"
file delete -force "$appsrc/main.c" "$appsrc/echo.c"
importsources -name s9la -path $src

app build -name s9la

# Запись NAND через JTAG (scripts/nand_write.tcl)
app create -name nandwrite -platform s9_la -domain $dom -template {Empty Application(C)}
importsources -name nandwrite -path [file normalize "$src/../../nandwrite/src"]
app build -name nandwrite
puts "OK"
