# S9 probe — JTAG, SWD, SPI, UART, I2C по сети

Работает одновременно с логическим анализатором (J1–J8) на свободных линиях S9.
Все линии — 3.3 В напрямую с ПЛИС. Для 1.8 В / 5 В нужны преобразователи уровня.

## Линии

Схема разъёмов с расположением пинов — [PINOUT.md](../../PINOUT.md).

| Линия | Разъём (пин) | Функция | Примечание |
|---|---|---|---|
| J9 TX | J9 (11) | TCK / SWCLK | |
| J9 RST | J9 (15) | TMS / SWDIO | |
| J9 PLUG | J9 (5) | TDI | |
| J9 RX | J9 (12) | TDO | |
| FAN1 SPEED | FAN1 (3) | nRESET цели | открытый сток, подтяжка 10 кОм |
| FAN2 SPEED | FAN2 (3) | UART TX (S9 → цель) | |
| FAN3 SPEED | FAN3 (3) | UART RX (цель → S9) | |
| FAN4 SPEED | FAN4 (3) | SPI SCK | |
| FAN5 SPEED | FAN5 (3) | SPI MOSI | |
| FAN6 SPEED | FAN6 (3) | SPI MISO | |
| SCL1 | J1–J8 (4) | SPI CS | подтяжка 1 кОм |
| SCL2 | J9 (4) | I2C SCL | подтяжка 1 кОм |
| SDA2 | J9 (3) | I2C SDA | подтяжка 1 кОм |
| SDA1 | J1–J8 (3) | резерв (GPIO) | подтяжка 1 кОм |
| — | J9 (1, 2, 9, 10, 13, 14), FAN (1) | GND | |

**На пине 2 разъёмов FAN — +12 В.** Шлейфом к FAN его не подключать.
J18 (пин 4 разъёмов FAN) — тестовый меандр анализатора.

## Сервисы

| Порт | Что | Чем пользоваться |
|---|---|---|
| 2542 | XVC (JTAG Xilinx) | Vivado: `open_hw_target -xvc_url <адрес>:2542`, XSCT: `connect -xvc-url TCP:<адрес>:2542` |
| 4441 | CMSIS-DAP (JTAG/SWD) | OpenOCD из git: `tools/openocd/build.sh`, конфиги `tools/openocd/s9probe-{swd,jtag}.cfg` |
| 5557 | serprog (SPI) | `flashrom -p serprog:ip=<адрес>:5557,spispeed=8M -r dump.bin` |
| 5558 | UART, RFC2217 | `python -m serial.tools.miniterm rfc2217://<адрес>:5558 115200` |
| 5559 | UART, сырой поток | `socat - TCP:<адрес>:5559` (скорость — последняя заданная) |
| 5560 | текстовая консоль | `nc <адрес> 5560`: `help`, `pins`, `jtag scan`, `i2c scan`, `reset pulse`… |
| 5561 | двоичный мост I2C/SPI | `s9bridge` → `/dev/i2c-10`, `/dev/spidev10.0`, `/dev/ttyETH0` |

Каждый сервис — один клиент; новое подключение вытесняет старое. Режимы делят одни
линии: JTAG/SWD (J9) и SPI (FAN4–6) можно использовать одновременно, разные клиенты JTAG/SWD
одновременно — нельзя.

Частота: JTAG/SWD/SPI до 16.7 МГц (полупериод ≥ 3 такта 100 МГц), UART до ~12 Мбод
(кадр 8N1), I2C программный до ~1 МГц.

## Устройства на ПК (s9bridge)

См. [tools/s9bridge/README.md](../../tools/s9bridge/README.md).

## Двоичный мост (TCP 5561)

```
запрос: u8 op, u8 0, u16 len, данные      ответ: u8 status (0 OK, 1 NACK, 2 ошибка), u8 0, u16 len, данные
op 0x01 I2C_XFER   u8 n; n × {u16 addr, u16 flags, u16 len, [данные записи]} → прочитанные байты
op 0x02 I2C_SPEED  u32 Гц → u32 фактическая
op 0x10 SPI_XFER   u32 Гц, u8 mode, u8 n; n × {u16 len, u8 flags (b0 tx, b1 rx, b2 cs_change), [tx]} → rx
op 0x20 PING
```
Флаги I2C — как в `linux/i2c.h` (I2C_M_RD, I2C_M_NOSTART, I2C_M_RECV_LEN, I2C_M_IGNORE_NAK).
