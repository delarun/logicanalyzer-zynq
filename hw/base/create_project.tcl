# =====================================================================
# Базовый проект Antminer S9 v1.3: PS7 + AXI GPIO
#   - DDR3 512 МБ (2× NT5CC128M16IP-DI, 32 бита)
#   - UART1 (MIO 48/49), SD0 (MIO 40-46, WP 50), GEM0 RGMII (MIO 16-27, MDIO 52/53),
#     NAND (MIO 0-14), GPIO MIO (светодиоды, пищалка, кнопки, enable хешплат)
#   - AXI GPIO: канал 1 — светодиоды D5..D8, канал 2 — 24 входа
#     {fan_tach[5:0], hb_rx[8:0], hb_plug[8:0]} для проверки ножек
#
# Запуск: vivado -mode batch -source hw/base/create_project.tcl -tclargs <build_dir>
# =====================================================================

set script_dir [file dirname [file normalize [info script]]]
set root       [file normalize "$script_dir/../.."]
if {[llength $argv] > 0} {
    set build_dir [file normalize [lindex $argv 0]]
} else {
    set build_dir "$root/build/hw_base"
}
set proj_name s9_base
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

# ---------------------------------------------------------------------
# Block design
# ---------------------------------------------------------------------
create_bd_design system

set ps [create_bd_cell -type ip -vlnv [ipvlnv processing_system7] ps7]
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "0" Master "Disable" Slave "Disable"} $ps

s9_ps7_config $ps

# AXI GPIO
set gpio [create_bd_cell -type ip -vlnv [ipvlnv axi_gpio] gpio]
set_property -dict [list \
    CONFIG.C_GPIO_WIDTH   {4}  CONFIG.C_ALL_OUTPUTS   {1} \
    CONFIG.C_IS_DUAL      {1}  \
    CONFIG.C_GPIO2_WIDTH  {24} CONFIG.C_ALL_INPUTS_2  {1} \
] $gpio

apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} Master {/ps7/M_AXI_GP0} Slave {/gpio/S_AXI} intc_ip {New AXI Interconnect} master_apm {0}} \
    [get_bd_intf_pins gpio/S_AXI]

# Внешние порты
create_bd_port -dir O -from 3 -to 0 led
connect_bd_net [get_bd_ports led] [get_bd_pins gpio/gpio_io_o]

set cc [create_bd_cell -type ip -vlnv [ipvlnv xlconcat] in_concat]
set_property -dict [list CONFIG.NUM_PORTS {3} CONFIG.IN0_WIDTH {9} CONFIG.IN1_WIDTH {9} CONFIG.IN2_WIDTH {6}] $cc
create_bd_port -dir I -from 8 -to 0 hb_plug
create_bd_port -dir I -from 8 -to 0 hb_rx
create_bd_port -dir I -from 5 -to 0 fan_tach
connect_bd_net [get_bd_ports hb_plug]  [get_bd_pins in_concat/In0]
connect_bd_net [get_bd_ports hb_rx]    [get_bd_pins in_concat/In1]
connect_bd_net [get_bd_ports fan_tach] [get_bd_pins in_concat/In2]
connect_bd_net [get_bd_pins in_concat/dout] [get_bd_pins gpio/gpio2_io_i]

assign_bd_address
validate_bd_design
save_bd_design

# ---------------------------------------------------------------------
# Wrapper, ограничения, сборка
# ---------------------------------------------------------------------
set wrapper [make_wrapper -files [get_files system.bd] -top]
add_files -norecurse $wrapper
set_property top system_wrapper [current_fileset]
add_files -fileset constrs_1 -norecurse "$script_dir/base.xdc"

launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Сборка impl_1 не завершилась"
}

file copy -force "$build_dir/$proj_name.runs/impl_1/system_wrapper.bit" "$build_dir/s9_base.bit"
write_hw_platform -fixed -include_bit -force -file "$build_dir/s9_base.xsa"
puts "OK: $build_dir/s9_base.xsa"
