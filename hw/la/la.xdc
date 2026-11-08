## Ограничения логического анализатора (выборка из board/s9_master.xdc)

## Светодиоды D5..D8
set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33} [get_ports {led[0]}]
set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports {led[1]}]
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports {led[2]}]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports {led[3]}]

## Входы хешплат: PLUG J1..J9
set_property -dict {PACKAGE_PIN T11 IOSTANDARD LVCMOS33} [get_ports {hb_plug[0]}]
set_property -dict {PACKAGE_PIN R19 IOSTANDARD LVCMOS33} [get_ports {hb_plug[1]}]
set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[2]}]
set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[3]}]
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[4]}]
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[5]}]
set_property -dict {PACKAGE_PIN T20 IOSTANDARD LVCMOS33} [get_ports {hb_plug[6]}]
set_property -dict {PACKAGE_PIN Y18 IOSTANDARD LVCMOS33} [get_ports {hb_plug[7]}]
set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[8]}]

## Входы хешплат: RX J1..J9
set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {hb_rx[0]}]
set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports {hb_rx[1]}]
set_property -dict {PACKAGE_PIN R14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[2]}]
set_property -dict {PACKAGE_PIN Y14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[3]}]
set_property -dict {PACKAGE_PIN W15 IOSTANDARD LVCMOS33} [get_ports {hb_rx[4]}]
set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports {hb_rx[5]}]
set_property -dict {PACKAGE_PIN W20 IOSTANDARD LVCMOS33} [get_ports {hb_rx[6]}]
set_property -dict {PACKAGE_PIN W16 IOSTANDARD LVCMOS33} [get_ports {hb_rx[7]}]
set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports {hb_rx[8]}]

## Тахометры вентиляторов FAN1..FAN6
set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33} [get_ports {fan_tach[0]}]
set_property -dict {PACKAGE_PIN F20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[1]}]
set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33} [get_ports {fan_tach[2]}]
set_property -dict {PACKAGE_PIN G18 IOSTANDARD LVCMOS33} [get_ports {fan_tach[3]}]
set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[4]}]
set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[5]}]

## I2C хешплат — только вход (SCL1, SDA1 для J1–J8; SCL2, SDA2 для J9)
set_property -dict {PACKAGE_PIN W18 IOSTANDARD LVCMOS33} [get_ports {i2c_in[0]}]
set_property -dict {PACKAGE_PIN W19 IOSTANDARD LVCMOS33} [get_ports {i2c_in[1]}]
set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports {i2c_in[2]}]
set_property -dict {PACKAGE_PIN P18 IOSTANDARD LVCMOS33} [get_ports {i2c_in[3]}]

## TX хешплат J1..J9 — выход (тестовые меандры, иначе 1)
set_property -dict {PACKAGE_PIN T12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[0]}]
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[1]}]
set_property -dict {PACKAGE_PIN P14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[2]}]
set_property -dict {PACKAGE_PIN W14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[3]}]
set_property -dict {PACKAGE_PIN V15 IOSTANDARD LVCMOS33} [get_ports {hb_tx[4]}]
set_property -dict {PACKAGE_PIN U18 IOSTANDARD LVCMOS33} [get_ports {hb_tx[5]}]
set_property -dict {PACKAGE_PIN V20 IOSTANDARD LVCMOS33} [get_ports {hb_tx[6]}]
set_property -dict {PACKAGE_PIN V16 IOSTANDARD LVCMOS33} [get_ports {hb_tx[7]}]
set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports {hb_tx[8]}]

## Входы асинхронны: первый триггер в IOB (атрибут в RTL), дальше синхронизатор
set_false_path -from [get_ports {hb_plug[*] hb_rx[*] fan_tach[*] i2c_in[*]}]
set_false_path -to   [get_ports {led[*] hb_tx[*]}]

set_property CFGBVS VCCO        [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
