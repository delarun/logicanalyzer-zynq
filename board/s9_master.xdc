## =====================================================================
## Antminer S9 control board v1.3 — мастер-файл ограничений PL
## Чип: XC7Z010-CLG400 (speed grade -1)  → part: xc7z010clg400-1
##
## Банки PL 34 и 35 питаются от 3.3 В → LVCMOS33.
## Направления IN/OUT взяты с распиновки платы: на линиях к хешплатам
## могут стоять буферы/резисторы, поэтому OUT-линии, скорее всего,
## нельзя использовать как входы (и наоборот). Проверить до использования!
##
## Использование: скопировать в проект, раскомментировать нужные строки.
## Индекс [0] у hb_* = разъём J1, ..., [8] = J9.
## =====================================================================

## ---------------- Разъёмы хешплат J1..J9 ----------------
## PLUG — вход (наличие платы), RST — выход, TX — выход, RX — вход
##                                         J#  сигнал    банк
#set_property -dict {PACKAGE_PIN T11 IOSTANDARD LVCMOS33} [get_ports {hb_plug[0]}] ;# J1 PLUG1 in
#set_property -dict {PACKAGE_PIN T10 IOSTANDARD LVCMOS33} [get_ports {hb_rst[0]}]  ;# J1 RST1  out
#set_property -dict {PACKAGE_PIN T12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[0]}]   ;# J1 TX1   out
#set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {hb_rx[0]}]   ;# J1 RX1   in

#set_property -dict {PACKAGE_PIN R19 IOSTANDARD LVCMOS33} [get_ports {hb_plug[1]}] ;# J2 PLUG2 in
#set_property -dict {PACKAGE_PIN V13 IOSTANDARD LVCMOS33} [get_ports {hb_rst[1]}]  ;# J2 RST2  out
#set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports {hb_tx[1]}]   ;# J2 TX2   out
#set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports {hb_rx[1]}]   ;# J2 RX2   in

#set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[2]}] ;# J3 PLUG3 in
#set_property -dict {PACKAGE_PIN T15 IOSTANDARD LVCMOS33} [get_ports {hb_rst[2]}]  ;# J3 RST3  out
#set_property -dict {PACKAGE_PIN P14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[2]}]   ;# J3 TX3   out
#set_property -dict {PACKAGE_PIN R14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[2]}]   ;# J3 RX3   in

#set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[3]}] ;# J4 PLUG4 in
#set_property -dict {PACKAGE_PIN Y17 IOSTANDARD LVCMOS33} [get_ports {hb_rst[3]}]  ;# J4 RST4  out
#set_property -dict {PACKAGE_PIN W14 IOSTANDARD LVCMOS33} [get_ports {hb_tx[3]}]   ;# J4 TX4   out
#set_property -dict {PACKAGE_PIN Y14 IOSTANDARD LVCMOS33} [get_ports {hb_rx[3]}]   ;# J4 RX4   in

#set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[4]}] ;# J5 PLUG5 in
#set_property -dict {PACKAGE_PIN U17 IOSTANDARD LVCMOS33} [get_ports {hb_rst[4]}]  ;# J5 RST5  out
#set_property -dict {PACKAGE_PIN V15 IOSTANDARD LVCMOS33} [get_ports {hb_tx[4]}]   ;# J5 TX5   out
#set_property -dict {PACKAGE_PIN W15 IOSTANDARD LVCMOS33} [get_ports {hb_rx[4]}]   ;# J5 RX5   in

#set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports {hb_plug[5]}] ;# J6 PLUG6 in
#set_property -dict {PACKAGE_PIN U15 IOSTANDARD LVCMOS33} [get_ports {hb_rst[5]}]  ;# J6 RST6  out
#set_property -dict {PACKAGE_PIN U18 IOSTANDARD LVCMOS33} [get_ports {hb_tx[5]}]   ;# J6 TX6   out
#set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports {hb_rx[5]}]   ;# J6 RX6   in

#set_property -dict {PACKAGE_PIN T20 IOSTANDARD LVCMOS33} [get_ports {hb_plug[6]}] ;# J7 PLUG7 in
#set_property -dict {PACKAGE_PIN U20 IOSTANDARD LVCMOS33} [get_ports {hb_rst[6]}]  ;# J7 RST7  out
#set_property -dict {PACKAGE_PIN V20 IOSTANDARD LVCMOS33} [get_ports {hb_tx[6]}]   ;# J7 TX7   out
#set_property -dict {PACKAGE_PIN W20 IOSTANDARD LVCMOS33} [get_ports {hb_rx[6]}]   ;# J7 RX7   in

#set_property -dict {PACKAGE_PIN Y18 IOSTANDARD LVCMOS33} [get_ports {hb_plug[7]}] ;# J8 PLUG8 in
#set_property -dict {PACKAGE_PIN Y19 IOSTANDARD LVCMOS33} [get_ports {hb_rst[7]}]  ;# J8 RST8  out
#set_property -dict {PACKAGE_PIN V16 IOSTANDARD LVCMOS33} [get_ports {hb_tx[7]}]   ;# J8 TX8   out
#set_property -dict {PACKAGE_PIN W16 IOSTANDARD LVCMOS33} [get_ports {hb_rx[7]}]   ;# J8 RX8   in

#set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports {hb_plug[8]}] ;# J9 PLUG9 in
#set_property -dict {PACKAGE_PIN R17 IOSTANDARD LVCMOS33} [get_ports {hb_rst[8]}]  ;# J9 RST9  out
#set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports {hb_tx[8]}]   ;# J9 TX9   out
#set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports {hb_rx[8]}]   ;# J9 RX9   in

## ---------------- I2C хешплат ----------------
## [0] — общая шина J1..J8, [1] — J9
#set_property -dict {PACKAGE_PIN W18 IOSTANDARD LVCMOS33} [get_ports {hb_scl[0]}] ;# SCL1 out (J1-J8)
#set_property -dict {PACKAGE_PIN W19 IOSTANDARD LVCMOS33} [get_ports {hb_sda[0]}] ;# SDA1 inout (J1-J8)
#set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports {hb_scl[1]}] ;# SCL2 out (J9)
#set_property -dict {PACKAGE_PIN P18 IOSTANDARD LVCMOS33} [get_ports {hb_sda[1]}] ;# SDA2 inout (J9)

## ---------------- Светодиоды (полярность не проверена) ----------------
#set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33} [get_ports {led[0]}] ;# D5
#set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports {led[1]}] ;# D6
#set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports {led[2]}] ;# D7
#set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports {led[3]}] ;# D8

## ---------------- Вентиляторы ----------------
## SPEED: соответствие разъёмам FAN1..6 проверено на плате (перемычка PWM→SPEED);
## на картинке распиновки FAN2–FAN5 перепутаны (там F20, G17, G18, J20 по порядку).
#set_property -dict {PACKAGE_PIN J18 IOSTANDARD LVCMOS33} [get_ports {fan_pwm}]     ;# FAN1-6 PWM out (общий)
#set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33} [get_ports {fan_tach[0]}] ;# FAN1 SPEED in
#set_property -dict {PACKAGE_PIN G18 IOSTANDARD LVCMOS33} [get_ports {fan_tach[1]}] ;# FAN2 SPEED in
#set_property -dict {PACKAGE_PIN F20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[2]}] ;# FAN3 SPEED in
#set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[3]}] ;# FAN4 SPEED in
#set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33} [get_ports {fan_tach[4]}] ;# FAN5 SPEED in
#set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports {fan_tach[5]}] ;# FAN6 SPEED in

## ---------------- Разное ----------------
#set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports {tp1_clk_out}] ;# CLOCK_OUT, тестовая точка TP1

## ID платы — резисторы-перемычки R183..R186 на обратной стороне (входы)
#set_property -dict {PACKAGE_PIN L14 IOSTANDARD LVCMOS33} [get_ports {board_id[0]}] ;# R183
#set_property -dict {PACKAGE_PIN L15 IOSTANDARD LVCMOS33} [get_ports {board_id[1]}] ;# R184
#set_property -dict {PACKAGE_PIN M14 IOSTANDARD LVCMOS33} [get_ports {board_id[2]}] ;# R185
#set_property -dict {PACKAGE_PIN M15 IOSTANDARD LVCMOS33} [get_ports {board_id[3]}] ;# R186

## ---------------- Конфигурация битстрима ----------------
#set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
#set_property CFGBVS VCCO        [current_design]
#set_property CONFIG_VOLTAGE 3.3 [current_design]
