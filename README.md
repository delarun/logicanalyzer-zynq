# logicanalyzer-zynq — сетевой логический анализатор на плате Antminer S9

Плата управления Antminer S9 (Zynq XC7Z010, 512 МБ DDR3, гигабитный Ethernet)
превращается в логический анализатор, который работает по сети с PulseView и sigrok-cli.

| | |
|---|---|
| Каналы | 32: RX, TX, RST, PLUG разъёмов хешплат J1–J8 (напрямую на ПЛИС) |
| Частота выборки | до 100 МГц (100 МГц / N) |
| Память | 384 МБ в DDR (до 384 млн выборок при 8 каналах) |
| Тестовый выход | меандр 1 кГц…1 МГц на пине 4 разъёмов FAN (J18) |
| Входная плата | схема переходника с защитой и буферами — [hw/frontend](hw/frontend/s9la_frontend.svg) |
| Отладка | JTAG, SWD, SPI, UART, I2C на свободных линиях (J9, FAN): XVC, CMSIS-DAP для OpenOCD, flashrom, RFC2217 — [sw/la/PROBE.md](sw/la/PROBE.md) |
| Устройства на ПК | `/dev/ttyETH0`, `/dev/i2c-10`, `/dev/spidev10.0` по сети — [tools/s9bridge](tools/s9bridge/README.md) |
| Триггер | аппаратный: уровни + фронты/спады, предзапись |
| Выгрузка | ~95 МБ/с по TCP (порт 5555), протокол — [sw/la/PROTOCOL.md](sw/la/PROTOCOL.md) |
| Клиенты | PulseView и sigrok-cli со своим драйвером `antminer-s9-la` |
| Загрузка | с NAND платы, без Linux (bare-metal + lwIP) |

**Что куда подключать — [PINOUT.md](PINOUT.md).** Описание платы и перемычки загрузки — [board/README.md](board/README.md).

## Что нужно

- Vivado/Vitis 2022.2 (XC7Z010 есть в бесплатной редакции).
- JTAG через Xilinx Virtual Cable: [pico-xvd](https://github.com/delarun/pico-xvd) —
  программатор на Raspberry Pi Pico (USB-сеть, XVC на `192.168.7.1:2542`, мост к UART S9).
- Для клиента: autotools, cmake, Qt6, glibmm, doxygen (сборка libsigrok и PulseView).

## Сборка и запуск

```sh
make la-sim       # симуляция ядра захвата (xsim)
make la-hw        # битстрим + XSA
make la-sw        # FSBL, прошивка s9la, писатель NAND
make la-jtag      # залить и запустить через JTAG (перемычки — JTAG)
make la-flash     # записать BOOT.BIN в NAND (перемычки — JTAG; потом JP2 и перезапуск питания)

tools/sigrok/build.sh                 # libsigrok с драйвером + PulseView + sigrok-cli в ~/.local/opt/sigrok-s9
tools/sigrok/pulseview.sh <ip платы>  # PulseView, сразу подключённый к анализатору
```

Адрес XVC задаётся переменной `XVC` (по умолчанию `TCP:192.168.7.1:2542`). Плата получает IP по
DHCP, MAC вычисляется из Device DNA кристалла — у каждой платы свой.

Проверка без PulseView:

```sh
tools/s9la/s9la.py <ip> id
tools/s9la/s9la.py <ip> selftest 50e6 4 100e6    # шаблон-счётчик: 200 МБ, проверка каждой выборки
~/.local/opt/sigrok-s9/bin/sigrok-cli -d antminer-s9-la:conn=<ip> --samples 1M --config samplerate=10M
```

## Структура

| Путь | Что |
|---|---|
| `hw/la/rtl/s9la_core.v` | ядро захвата: синхронизация входов, делитель, триггер, упаковка, FIFO, AXI-запись в DDR |
| `hw/la/` | проект Vivado анализатора, ограничения, тестбенч |
| `hw/common/ps7_s9.tcl` | общая конфигурация PS7 (DDR, MIO, тайминги NAND) |
| `sw/la/` | прошивка: lwIP, TCP-сервер, выгрузка из DDR без копирования |
| `sw/nandwrite/`, `scripts/nand_write.tcl` | запись BOOT.BIN в NAND через JTAG (program_flash с XVC не работает) |
| `sw/patches/` | патч lwIP: определение скорости для PHY Broadcom на S9 |
| `tools/sigrok/` | драйвер libsigrok и сборка PulseView/sigrok-cli |
| `tools/s9la/` | клиент на Python для проверки |
| `hw/la/rtl/s9probe.v`, `sw/la/src/svc_*.c` | движок JTAG/SWD/SPI, UART, GPIO и сетевые сервисы |
| `tools/s9bridge/` | служба ПК: устройства в /dev через PTY и CUSE |
| `tools/openocd/` | сборка OpenOCD с CMSIS-DAP по TCP и конфиги |
| `hw/base/`, `sw/base/` | базовый проект: эхо-сервер lwIP и тест памяти |
| `board/` | справочник по плате S9 v1.3 |

## Особенности платы S9, найденные по ходу

- PHY Ethernet — Broadcom (ID `0362:5E62`): стандартный драйвер lwIP Xilinx определяет скорость
  неверно, нужен патч из `sw/patches`.
- NAND требует медленных таймингов SMC (как в стоковом U-Boot, `set_cycles = 0x00449144`) —
  с таймингами Vivado по умолчанию ONFI не распознаётся.
- Vitis 2022.2 на свежих дистрибутивах: если xsct падает с `channel tcfchan#0 closed`, переименуй
  `Vitis/2022.2/lib/lnx64.o/Default/libstdc++.so.6` (старая libstdc++ ломает GTK/SWT).

## Лицензия

[GPL-3.0](LICENSE). Драйвер `tools/sigrok/driver` — часть libsigrok (GPL-3.0-or-later).
