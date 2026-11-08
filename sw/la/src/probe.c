#include "probe.h"

#include "xil_printf.h"
#include "xtime_l.h"

#define R_ID    0x00
#define R_CTRL  0x04
#define R_DIV   0x08
#define R_GPO   0x0C
#define R_GOE   0x10
#define R_GPI   0x14
#define R_CMD   0x18
#define R_RES   0x1C
#define R_STAT  0x20
#define R_UDIV  0x24
#define R_UTX   0x28
#define R_URX   0x2C

#define CTRL_JTAG 1u
#define CTRL_SWD  2u
#define CTRL_SPI  4u
#define CTRL_UART 8u

#define CLK_HZ    100000000u
#define INFLIGHT  48            // команд в FIFO движка одновременно (глубина 64)

static u32 g_ctrl, g_div = 4, g_gpo, g_goe;
static int g_mode = PM_NONE;

static inline void wr(u32 off, u32 v) { Xil_Out32(PRB_BASE + off, v); }
static inline u32  rd(u32 off)        { return Xil_In32(PRB_BASE + off); }

void probe_delay_ns(u32 ns)
{
    XTime t0, t;
    XTime_GetTime(&t0);
    u64 ticks = ((u64)ns * COUNTS_PER_SECOND) / 1000000000u + 1;
    do { XTime_GetTime(&t); } while (t - t0 < ticks);
}

void probe_delay_us(u32 us)
{
    while (us > 1000) { probe_delay_ns(1000000); us -= 1000; }
    probe_delay_ns(us * 1000);
}

void probe_init(void)
{
    if (rd(R_ID) != 0x50524231u) {
        xil_printf("probe: модуль не найден (ID %08x)\r\n", (unsigned)rd(R_ID));
        return;
    }
    g_ctrl = 0; g_gpo = 0; g_goe = 0;
    wr(R_CTRL, 0);
    wr(R_GPO, 0);
    wr(R_GOE, 0);
    wr(R_DIV, g_div);
    // CS SPI в покое — высокий, выход
    probe_gpio(1u << PIN_CS, 1u << PIN_CS, 1u << PIN_CS);
    xil_printf("probe: JTAG/SWD — J9, UART/SPI — FAN2..6, I2C — J9 SCL2/SDA2\r\n");
}

u32 probe_hz_actual(u32 hz)
{
    if (hz == 0) hz = 1;
    u32 half = (CLK_HZ / 2 + hz - 1) / hz;      // тактов на период → округление вверх
    u32 div = (half + 1) / 2;                   // полупериод
    if (div < 3) div = 3;
    if (div > 65536) div = 65536;
    return CLK_HZ / (2 * div);
}

static void set_hz(u32 hz)
{
    if (hz == 0) return;
    u32 per = (CLK_HZ + hz - 1) / hz;           // тактов aclk на период
    u32 half = (per + 1) / 2;
    if (half < 3) half = 3;
    if (half > 65536) half = 65536;
    if (half - 1 != g_div) {
        g_div = half - 1;
        wr(R_DIV, g_div);
    }
}

u32 probe_freq(void)
{
    return CLK_HZ / (2 * (g_div + 1));
}

void probe_mode(int mode, u32 hz)
{
    u32 bits = g_ctrl & CTRL_UART;
    if (mode == PM_JTAG) bits |= CTRL_JTAG;
    else if (mode == PM_SWD) bits |= CTRL_SWD;
    else if (mode == PM_SPI) bits |= CTRL_SPI;
    if (bits != g_ctrl) {
        g_ctrl = bits;
        wr(R_CTRL, g_ctrl);
    }
    g_mode = mode;
    set_hz(hz);
}

static inline u8 res_pop(void)
{
    u32 v;
    do { v = rd(R_RES); } while (!(v & 0x80000000u));
    return (u8)v;
}

static inline u32 cmd_word(int mode, u32 nbits, u32 k, const u8 *a, const u8 *b,
                           const u8 *aoe, int want)
{
    u32 nb = nbits - k * 8;
    if (nb > 8) nb = 8;
    return (a ? a[k] : 0u)
         | (u32)(b ? b[k] : 0xFFu) << 8
         | (u32)(aoe ? aoe[k] : 0xFFu) << 16
         | (nb - 1) << 24
         | (u32)mode << 27
         | (want ? 0u : 1u << 29);
}

void probe_shift(int mode, u32 nbits, const u8 *a, const u8 *b, const u8 *aoe, u8 *in)
{
    u32 ncmd = (nbits + 7) / 8;
    u32 sent = 0;

    if (!in) {
        // без ответов: следим только за свободным местом в FIFO команд
        u32 room = 0;
        while (sent < ncmd) {
            if (room == 0) {
                u32 used = rd(R_STAT) & 0x7F;
                room = used < 60 ? 60 - used : 0;
                continue;
            }
            wr(R_CMD, cmd_word(mode, nbits, sent, a, b, aoe, 0));
            sent++;
            room--;
        }
        // дождаться окончания (важно перед сменой GPIO/CS)
        while (rd(R_STAT) & (0x7Fu | (1u << 16)))
            ;
        return;
    }

    u32 got = 0;
    while (got < ncmd) {
        while (sent < ncmd && sent - got < INFLIGHT) {
            wr(R_CMD, cmd_word(mode, nbits, sent, a, b, aoe, 1));
            sent++;
        }
        in[got++] = res_pop();
    }
    if (nbits % 8)
        in[ncmd - 1] &= (u8)((1u << (nbits % 8)) - 1);
}

u32 probe_shift32(int mode, u32 nbits, u32 a, u32 b, u32 aoe, int read)
{
    u8 ab[4] = {a, a >> 8, a >> 16, a >> 24};
    u8 bb[4] = {b, b >> 8, b >> 16, b >> 24};
    u8 ob[4] = {aoe, aoe >> 8, aoe >> 16, aoe >> 24};
    u8 ib[4] = {0, 0, 0, 0};
    probe_shift(mode, nbits, ab, bb, ob, read ? ib : NULL);
    return ib[0] | (ib[1] << 8) | (ib[2] << 16) | ((u32)ib[3] << 24);
}

void probe_gpio(u32 mask, u32 val, u32 oe)
{
    g_gpo = (g_gpo & ~mask) | (val & mask);
    g_goe = (g_goe & ~mask) | (oe & mask);
    wr(R_GPO, g_gpo);
    wr(R_GOE, g_goe);
}

u32 probe_gpio_in(void)
{
    return rd(R_GPI);
}

void probe_od(int pin, int level)
{
    u32 m = 1u << pin;
    probe_gpio(m, 0, level ? 0 : m);
}

void probe_uart_enable(int on)
{
    if (on) g_ctrl |= CTRL_UART; else g_ctrl &= ~CTRL_UART;
    wr(R_CTRL, g_ctrl);
}

u32 probe_uart_baud(u32 baud)
{
    if (baud < 300) baud = 300;
    u32 div = (CLK_HZ + baud / 2) / baud;
    if (div < 8) div = 8;
    if (div > 65535) div = 65535;
    wr(R_UDIV, div);
    return CLK_HZ / div;
}

int probe_uart_tx_free(void)
{
    return (int)(rd(R_UTX) & 0x7F);
}

void probe_uart_put(u8 c)
{
    wr(R_UTX, c);
}

int probe_uart_get(void)
{
    u32 v = rd(R_URX);
    return (v & 0x80000000u) ? (int)(v & 0xFF) : -1;
}
