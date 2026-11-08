## Ограничения логического анализатора S9 (ножки — по схеме платы V1.01, листы 6 и 13)

## Светодиоды D5..D8
set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33} [get_ports {led[0]}]
set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports {led[1]}]
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports {led[2]}]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports {led[3]}]

## Входы: разъёмы хешплат J1..J8 (RX, TX, RST, PLUG — напрямую с банка 34)
## J1
set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {hb_rx[0]}]
set_property -dict {PACKAGE_PIN T12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[0]}]
set_property -dict {PACKAGE_PIN T10 IOSTANDARD LVCMOS33} [get_ports {hb_rst[0]}]
set_property -dict {PACKAGE_PIN T11 IOSTANDARD LVCMOS33} [get_ports {hb_plug[0]}]
## J2
set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports {hb_rx[1]}]
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[1]}]
set_property -dict {PACKAGE_PIN V13 IOSTANDARD LVCMOS33} [get_ports {hb_rst[1]}]
set_property -dict {PACKAGE_PIN R19 IOSTANDARD LVCMOS33} [get_ports {hb_plug[1]}]
## J3
set_property -dict {PACKAGE_PIN R14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[2]}]
set_property -dict {PACKAGE_PIN P14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[2]}]
set_property -dict {PACKAGE_PIN T15 IOSTANDARD LVCMOS33} [get_ports {hb_rst[2]}]
set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[2]}]
## J4
set_property -dict {PACKAGE_PIN Y14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[3]}]
set_property -dict {PACKAGE_PIN W14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[3]}]
set_property -dict {PACKAGE_PIN Y17 IOSTANDARD LVCMOS33} [get_ports {hb_rst[3]}]
set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[3]}]
## J5
set_property -dict {PACKAGE_PIN W15 IOSTANDARD LVCMOS33} [get_ports {hb_rx[4]}]
set_property -dict {PACKAGE_PIN V15 IOSTANDARD LVCMOS33} [get_ports {hb_tx[4]}]
set_property -dict {PACKAGE_PIN U17 IOSTANDARD LVCMOS33} [get_ports {hb_rst[4]}]
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[4]}]
## J6
set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports {hb_rx[5]}]
set_property -dict {PACKAGE_PIN U18 IOSTANDARD LVCMOS33} [get_ports {hb_tx[5]}]
set_property -dict {PACKAGE_PIN U15 IOSTANDARD LVCMOS33} [get_ports {hb_rst[5]}]
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[5]}]
## J7
set_property -dict {PACKAGE_PIN W20 IOSTANDARD LVCMOS33} [get_ports {hb_rx[6]}]
set_property -dict {PACKAGE_PIN V20 IOSTANDARD LVCMOS33} [get_ports {hb_tx[6]}]
set_property -dict {PACKAGE_PIN U20 IOSTANDARD LVCMOS33} [get_ports {hb_rst[6]}]
set_property -dict {PACKAGE_PIN T20 IOSTANDARD LVCMOS33} [get_ports {hb_plug[6]}]
## J8
set_property -dict {PACKAGE_PIN W16 IOSTANDARD LVCMOS33} [get_ports {hb_rx[7]}]
set_property -dict {PACKAGE_PIN V16 IOSTANDARD LVCMOS33} [get_ports {hb_tx[7]}]
set_property -dict {PACKAGE_PIN Y19 IOSTANDARD LVCMOS33} [get_ports {hb_rst[7]}]
set_property -dict {PACKAGE_PIN Y18 IOSTANDARD LVCMOS33} [get_ports {hb_plug[7]}]

## Тестовые выходы: J9 TX, J9 RST, J18 (PWM вентиляторов, пин 4 разъёмов FAN)
set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports test_tx]
set_property -dict {PACKAGE_PIN R17 IOSTANDARD LVCMOS33} [get_ports test_rst]
set_property -dict {PACKAGE_PIN J18 IOSTANDARD LVCMOS33} [get_ports fan_pwm]

## Входы асинхронны: первый триггер в IOB (атрибут в RTL), дальше синхронизатор
set_false_path -from [get_ports {hb_rx[*] hb_tx[*] hb_rst[*] hb_plug[*]}]
set_false_path -to   [get_ports {led[*] test_tx test_rst fan_pwm}]

set_property CFGBVS VCCO        [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
