# =====================================================================
# Логический анализатор на Antminer S9 v1.3
#   PS7 (как в базовом проекте) + s9la_core (RTL, module reference)
#   - регистры ядра: M_AXI_GP0 → 0x43C0_0000
#   - запись выборок: s9la_core/m_axi → S_AXI_HP0 → DDR
#   - FCLK0 100 МГц: и выборка, и шины
#
# Запуск: vivado -mode batch -source hw/la/create_project.tcl -tclargs <build_dir>
# =====================================================================

set script_dir [file dirname [file normalize [info script]]]
set root       [file normalize "$script_dir/../.."]
if {[llength $argv] > 0} {
    set build_dir [file normalize [lindex $argv 0]]
} else {
    set build_dir "$root/build/hw_la"
}
set proj_name s9_la
set part      xc7z010clg400-1
set jobs      [expr {[info exists ::env(JOBS)] ? $::env(JOBS) : 8}]

source "$root/hw/common/ps7_s9.tcl"

proc ipvlnv {name} {
    set defs [lsort [get_ipdefs -all xilinx.com:ip:${name}:*]]
    if {[llength $defs] == 0} { error "IP $name не найден" }
    return [lindex $defs end]
}

create_project $proj_name $build_dir -part $part -force
set_property target_language Verilog [current_project]
add_files -norecurse [list "$script_dir/rtl/s9la_core.v" "$script_dir/rtl/s9probe.v"]
update_compile_order -fileset sources_1

# ---------------------------------------------------------------------
# Block design
# ---------------------------------------------------------------------
create_bd_design system

set ps [create_bd_cell -type ip -vlnv [ipvlnv processing_system7] ps7]
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "0" Master "Disable" Slave "Disable"} $ps
s9_ps7_config $ps
set_property -dict [list \
    CONFIG.PCW_USE_S_AXI_HP0         {1} \
    CONFIG.PCW_S_AXI_HP0_DATA_WIDTH  {64} \
] $ps

set la [create_bd_cell -type module -reference s9la_core la]
set probe [create_bd_cell -type module -reference s9probe probe]

# Регистры: GP0 → AXI-Lite ядра
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Clk_master {/ps7/FCLK_CLK0 (100 MHz)} Clk_slave {/ps7/FCLK_CLK0 (100 MHz)} Clk_xbar {/ps7/FCLK_CLK0 (100 MHz)} Master {/ps7/M_AXI_GP0} Slave {/la/s_axi} intc_ip {New AXI Interconnect} master_apm {0}} \
    [get_bd_intf_pins la/s_axi]

# Регистры отладочных интерфейсов: тот же GP0
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Clk_master {/ps7/FCLK_CLK0 (100 MHz)} Clk_slave {/ps7/FCLK_CLK0 (100 MHz)} Clk_xbar {/ps7/FCLK_CLK0 (100 MHz)} Master {/ps7/M_AXI_GP0} Slave {/probe/s_axi} intc_ip {Auto} master_apm {0}} \
    [get_bd_intf_pins probe/s_axi]

# Данные: m_axi ядра → HP0 (протокол AXI4 → AXI3 преобразует interconnect)
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Clk_master {/ps7/FCLK_CLK0 (100 MHz)} Clk_slave {/ps7/FCLK_CLK0 (100 MHz)} Clk_xbar {/ps7/FCLK_CLK0 (100 MHz)} Master {/la/m_axi} Slave {/ps7/S_AXI_HP0} intc_ip {New AXI Interconnect} master_apm {0}} \
    [get_bd_intf_pins ps7/S_AXI_HP0]

# Внешние порты
foreach {name dir width} {
    hb_rx    I 8
    hb_tx    I 8
    hb_rst   I 8
    hb_plug  I 8
    fan_pwm  O 1
    led      O 4
} {
    if {$width == 1} {
        create_bd_port -dir $dir $name
    } else {
        create_bd_port -dir $dir -from [expr {$width - 1}] -to 0 $name
    }
    connect_bd_net [get_bd_ports $name] [get_bd_pins la/$name]
}

# Линии probe: выход/разрешение/вход наружу, IOBUF — в верхнем модуле
foreach {name dir} {pin_o O pin_oe O pin_i I} {
    create_bd_port -dir $dir -from 13 -to 0 probe_$name
    connect_bd_net [get_bd_ports probe_$name] [get_bd_pins probe/$name]
}

assign_bd_address
# Фиксированные адреса регистров — прошивка использует их напрямую
foreach {pat addr} {"*SEG_la_*" 0x43C00000 "*SEG_probe_*" 0x43C10000} {
    set seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps7/Data] -filter "NAME =~ $pat"]
    set_property offset $addr $seg
    set_property range  64K   $seg
    puts "Регистры [get_property NAME $seg]: [get_property offset $seg]"
}

validate_bd_design
save_bd_design

# ---------------------------------------------------------------------
# Wrapper, ограничения, сборка
# ---------------------------------------------------------------------
set wrapper [make_wrapper -files [get_files system.bd] -top]
add_files -norecurse $wrapper

# Верхний модуль: порты обёртки как есть, а probe_pin_o/oe/i → inout probe[13:0] через IOBUF
set fh [open $wrapper r]; set wtext [read $fh]; close $fh
set ports {}
foreach line [split $wtext "\n"] {
    if {[regexp {^\s*(input|output|inout)\s*(\[[0-9]+:[0-9]+\])?\s*(\w+);} $line -> dir rng name]} {
        lappend ports [list $dir $rng $name]
    }
}
set top "$build_dir/s9la_top.v"
set fh [open $top w]
puts $fh "// Сгенерировано hw/la/create_project.tcl — не редактировать"
puts $fh "module s9la_top ("
set decl {}; set conn {}
foreach p $ports {
    lassign $p dir rng name
    if {[string match probe_pin_* $name]} continue
    lappend decl "    $dir $rng $name"
    lappend conn "    .$name\($name\)"
}
lappend decl "    inout \[13:0\] probe"
puts $fh "[join $decl ",\n"]\n);"
puts $fh "    wire \[13:0\] probe_pin_o, probe_pin_oe, probe_pin_i;"
puts $fh "    system_wrapper u_sys (\n[join $conn ",\n"],"
puts $fh "    .probe_pin_o(probe_pin_o), .probe_pin_oe(probe_pin_oe), .probe_pin_i(probe_pin_i));"
puts $fh "    genvar i;"
puts $fh "    generate for (i = 0; i < 14; i = i + 1) begin : g_io"
puts $fh "        IOBUF u_io (.IO(probe\[i\]), .I(probe_pin_o\[i\]), .T(!probe_pin_oe\[i\]), .O(probe_pin_i\[i\]));"
puts $fh "    end endgenerate"
puts $fh "endmodule"
close $fh
add_files -norecurse $top
set_property top s9la_top [current_fileset]
add_files -fileset constrs_1 -norecurse "$script_dir/la.xdc"

launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Сборка impl_1 не завершилась"
}

open_run impl_1
report_timing_summary -file "$build_dir/timing.rpt"
report_utilization    -file "$build_dir/utilization.rpt"
set wns [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup]]
puts "WNS: $wns нс"
if {$wns < 0} { error "Тайминг не сошёлся: WNS = $wns" }

file copy -force "$build_dir/$proj_name.runs/impl_1/s9la_top.bit" "$build_dir/s9_la.bit"
write_hw_platform -fixed -include_bit -force -file "$build_dir/s9_la.xsa"
puts "OK: $build_dir/s9_la.xsa"
