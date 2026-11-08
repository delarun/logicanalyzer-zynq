// Программный I2C-мастер на линиях SCL2/SDA2 (J9), открытый сток, подтяжки 1 кОм на плате.
#pragma once
#include "xil_types.h"

#define I2C_M_RD        0x0001
#define I2C_M_TEN       0x0010
#define I2C_M_RECV_LEN  0x0400
#define I2C_M_IGNORE_NAK 0x1000
#define I2C_M_NOSTART   0x4000

u32  i2c_speed(u32 hz);                 // возвращает фактическую
void i2c_start(void);
void i2c_stop(void);
int  i2c_wbyte(u8 b);                   // 0 — ACK
u8   i2c_rbyte(int ack);
int  i2c_probe(u8 addr7);               // 0 — устройство ответило
