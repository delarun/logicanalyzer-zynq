// Драйвер s9probe (hw/la/rtl/s9probe.v): сдвиговый движок JTAG/SWD/SPI, UART, GPIO.
#pragma once
#include "xil_io.h"
#include "xil_types.h"

#define PRB_BASE    0x43C10000u

// Линии
#define PIN_TCK     0   // J9 TX     — TCK / SWCLK
#define PIN_TMS     1   // J9 RST    — TMS / SWDIO
#define PIN_TDI     2   // J9 PLUG   — TDI
#define PIN_TDO     3   // J9 RX     — TDO
#define PIN_NRST    4   // FAN1      — nRESET (открытый сток, подтяжка 10 кОм на плате)
#define PIN_UTX     5   // FAN2      — UART TX
#define PIN_URX     6   // FAN3      — UART RX
#define PIN_SCK     7   // FAN4      — SPI SCK
#define PIN_MOSI    8   // FAN5      — SPI MOSI
#define PIN_MISO    9   // FAN6      — SPI MISO
#define PIN_CS      10  // SCL1      — SPI CS (подтяжка 1 кОм)
#define PIN_SCL     11  // SCL2 (J9) — I2C SCL (подтяжка 1 кОм)
#define PIN_SDA     12  // SDA2 (J9) — I2C SDA (подтяжка 1 кОм)
#define PIN_AUX     13  // SDA1      — резерв
#define PIN_COUNT   14

enum probe_mode { PM_NONE = -1, PM_JTAG = 0, PM_SWD = 1, PM_SPI = 2 };

void probe_init(void);
void probe_mode(int mode, u32 hz);        // движок в режим + частота такта (0 — не менять)
u32  probe_freq(void);                    // текущая частота такта, Гц
u32  probe_hz_actual(u32 hz);             // ближайшая достижимая частота

// Сдвиг nbits бит младшим первым. a/b/aoe/in — побитовые массивы (байт k — биты 8k..8k+7).
// a = NULL — нули, b = NULL — единицы, aoe = NULL — выход всегда включён, in = NULL — не читать.
void probe_shift(int mode, u32 nbits, const u8 *a, const u8 *b, const u8 *aoe, u8 *in);
u32  probe_shift32(int mode, u32 nbits, u32 a, u32 b, u32 aoe, int read);

// GPIO: биты по номерам линий
void probe_gpio(u32 mask, u32 val, u32 oe);   // для линий из mask: уровень val, выход oe
u32  probe_gpio_in(void);
void probe_od(int pin, int level);            // открытый сток: 0 — тянуть вниз, 1 — отпустить

// UART
void probe_uart_enable(int on);
u32  probe_uart_baud(u32 baud);               // возвращает фактическую
int  probe_uart_tx_free(void);
void probe_uart_put(u8 c);
int  probe_uart_get(void);                    // -1 — нет данных

void probe_delay_us(u32 us);
void probe_delay_ns(u32 ns);

static inline u8 rev8(u8 b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}
