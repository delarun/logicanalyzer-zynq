# s9bridge — интерфейсы S9 probe как устройства Linux

| Файл | Что | Чем работать |
|---|---|---|
| `/dev/ttyETH0` | UART (FAN2 TX, FAN3 RX) | picocom, minicom, screen, pyserial; скорость — из настроек терминала |
| `/dev/i2c-10` | I2C (J9 SCL2/SDA2) | `i2cdetect -y 10`, `i2cget`, `i2cset`, `i2ctransfer`, Python smbus2 |
| `/dev/spidev10.0` | SPI (FAN4–6 + SCL1) | `spidev_test`, Python spidev, `flashrom -p linux_spi:dev=/dev/spidev10.0` |

UART — псевдотерминал (PTY), I2C и SPI — символьные устройства через CUSE (libfuse3) с
теми же ioctl, что у i2c-dev и spidev. Драйверы ядра к этим шинам не подключаются — только
программы.

К плате обращается **только пока открыт хотя бы один из файлов**: соединение с S9 создаётся
при открытии и закрывается после последнего закрытия. В простое процесс спит в ядре и
не тратит CPU.

## Установка

```sh
cd tools/s9bridge
make
sudo make install                 # /usr/local/bin/s9bridge, служба systemd, /etc/s9bridge.conf
sudoedit /etc/s9bridge.conf       # адрес S9 (host = ...)
sudo systemctl enable --now s9bridge
```

Нужны пакеты `fuse3` (сборка) и модуль ядра `cuse` (служба загружает его сама).
Без службы: `sudo modprobe cuse && sudo s9bridge`.

## /etc/s9bridge.conf

```
host = 192.168.10.253
tty  = /dev/ttyETH0     # off — не создавать
i2c  = 10               # /dev/i2c-10, off — не создавать
spi  = 10.0             # /dev/spidev10.0, off — не создавать
```

Аргументы командной строки перекрывают файл: `s9bridge [адрес] [--config файл] [--tty путь]
[--i2c N] [--spi N.M] [--no-tty] [--no-i2c] [--no-spi]`.

## Ограничения

- SPI: 8 бит на слово, режимы 0 и 3 (CPOL/CPHA 1 и 2 не поддерживаются движком), до 16 МГц,
  до 16 КБ на одно сообщение. LSB-first делается разворотом на ПК.
- I2C: 7-битные адреса, программный мастер на S9 (до ~1 МГц), до 42 сообщений в I2C_RDWR.
- UART: 8N1; другие настройки RFC2217 подтверждаются как 8N1.
- 32-битные программы (FUSE_IOCTL_COMPAT) не поддерживаются.
