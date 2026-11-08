# logicanalyzer-zynq — сетевой логический анализатор на плате Antminer S9

Плата управления Antminer S9 (Zynq XC7Z010, 512 МБ DDR3, гигабитный Ethernet)
превращается в логический анализатор, который работает по сети с PulseView и sigrok-cli.

| | |
|---|---|
| Каналы | 28 (RX и PLUG хешплат J1–J9, FAN1–6, I2C) |
| Частота выборки | до 100 МГц (100 МГц / N) |
| Память | 384 МБ в DDR (до 384 млн выборок при 8 каналах) |
| Триггер | аппаратный: уровни + фронты/спады, предзапись |
| Выгрузка | ~95 МБ/с по TCP (порт 5555), протокол — [sw/la/PROTOCOL.md](sw/la/PROTOCOL.md) |
| Клиенты | PulseView и sigrok-cli со своим драйвером `antminer-s9-la` |
| Загрузка | с NAND платы, без Linux (bare-metal + lwIP) |

Описание платы, распиновка и перемычки загрузки — [board/README.md](board/README.md).

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
| `hw/base/`, `sw/base/` | базовый проект: эхо-сервер lwIP и тест памяти |
| `board/` | справочник по плате S9 v1.3 |

## Особенности платы S9, найденные по ходу

- PHY Ethernet — Broadcom (ID `0362:5E62`): стандартный драйвер lwIP Xilinx определяет скорость
  неверно, нужен патч из `sw/patches`.
- NAND требует медленных таймингов SMC (как в стоковом U-Boot, `set_cycles = 0x00449144`) —
  с таймингами Vivado по умолчанию ONFI не распознаётся.
- Vitis 2022.2 на свежих дистрибутивах: если xsct падает с `channel tcfchan#0 closed`, переименуй
  `Vitis/2022.2/lib/lnx64.o/Default/libstdc++.so.6` (старая libstdc++ ломает GTK/SWT).
