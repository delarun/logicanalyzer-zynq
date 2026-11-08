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

## Тестовый выход: J18 (PWM вентиляторов, пин 4 разъёмов FAN)
set_property -dict {PACKAGE_PIN J18 IOSTANDARD LVCMOS33} [get_ports fan_pwm]

## Отладочные интерфейсы (s9probe): JTAG/SWD, UART, SPI, I2C, GPIO
set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports {probe[0]}] ;# J9 TX — TCK/SWCLK
set_property -dict {PACKAGE_PIN R17 IOSTANDARD LVCMOS33} [get_ports {probe[1]}] ;# J9 RST — TMS/SWDIO
set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports {probe[2]}] ;# J9 PLUG — TDI
set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports {probe[3]}] ;# J9 RX — TDO
set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33} [get_ports {probe[4]}] ;# FAN1 SPEED — nRESET
set_property -dict {PACKAGE_PIN G18 IOSTANDARD LVCMOS33} [get_ports {probe[5]}] ;# FAN2 SPEED — UART TX
set_property -dict {PACKAGE_PIN F20 IOSTANDARD LVCMOS33} [get_ports {probe[6]}] ;# FAN3 SPEED — UART RX
set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33} [get_ports {probe[7]}] ;# FAN4 SPEED — SPI SCK
set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33} [get_ports {probe[8]}] ;# FAN5 SPEED — SPI MOSI
set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports {probe[9]}] ;# FAN6 SPEED — SPI MISO
set_property -dict {PACKAGE_PIN W18 IOSTANDARD LVCMOS33} [get_ports {probe[10]}] ;# SCL1 (J1–J8 пин 4) — SPI CS
set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports {probe[11]}] ;# SCL2 (J9 пин 4) — I2C SCL
set_property -dict {PACKAGE_PIN P18 IOSTANDARD LVCMOS33} [get_ports {probe[12]}] ;# SDA2 (J9 пин 3) — I2C SDA
set_property -dict {PACKAGE_PIN W19 IOSTANDARD LVCMOS33} [get_ports {probe[13]}] ;# SDA1 (J1–J8 пин 3) — резерв

## Входы асинхронны: первый триггер в IOB (атрибут в RTL), дальше синхронизатор
set_false_path -from [get_ports {hb_rx[*] hb_tx[*] hb_rst[*] hb_plug[*]}]
set_false_path -to   [get_ports {led[*] fan_pwm}]
# probe: синхронные интерфейсы на низкой частоте, тайминг держит делитель
set_false_path -from [get_ports {probe[*]}]
set_false_path -to   [get_ports {probe[*]}]

set_property CFGBVS VCCO        [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
