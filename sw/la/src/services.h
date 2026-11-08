// Сетевые сервисы отладочных интерфейсов (s9probe)
#pragma once

void svc_xvc_init(void);      // TCP 2542 — XVC (JTAG для Vivado/XSCT)
void svc_xvc_poll(void);
void svc_dap_init(void);      // TCP 4441 — CMSIS-DAP для OpenOCD (JTAG/SWD)
void svc_dap_poll(void);
void svc_serprog_init(void);  // TCP 5557 — serprog для flashrom (SPI)
void svc_serprog_poll(void);
void svc_uart_init(void);     // TCP 5558 — RFC2217, TCP 5559 — сырой поток
void svc_uart_poll(void);
void svc_cmd_init(void);      // TCP 5560 — текстовая консоль: I2C, GPIO, сброс, JTAG-скан
void svc_cmd_poll(void);
void svc_bridge_init(void);   // TCP 5561 — двоичный мост I2C/SPI для s9bridge (/dev/i2c-N, /dev/spidev)
void svc_bridge_poll(void);
