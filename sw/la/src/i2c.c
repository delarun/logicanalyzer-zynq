#include "i2c.h"

#include "probe.h"

static u32 half_ns = 5000;     // 100 кГц

static void scl(int v) { probe_od(PIN_SCL, v); }
static void sda(int v) { probe_od(PIN_SDA, v); }
static int  sda_in(void) { return (probe_gpio_in() >> PIN_SDA) & 1; }
static void dly(void) { probe_delay_ns(half_ns); }

static void scl_high(void)
{
    scl(1);
    for (int i = 0; i < 1000 && !((probe_gpio_in() >> PIN_SCL) & 1); i++)   // растяжение такта
        probe_delay_us(1);
    dly();
}

u32 i2c_speed(u32 hz)
{
    if (hz < 1000) hz = 1000;
    if (hz > 1000000) hz = 1000000;
    half_ns = 500000000u / hz;
    return 500000000u / half_ns;
}

void i2c_start(void) { sda(1); scl_high(); sda(0); dly(); scl(0); dly(); }
void i2c_stop(void)  { sda(0); dly(); scl_high(); sda(1); dly(); }

int i2c_wbyte(u8 b)
{
    for (int i = 7; i >= 0; i--) {
        sda((b >> i) & 1); dly();
        scl_high(); scl(0);
    }
    sda(1); dly();
    scl_high();
    int nack = sda_in();
    scl(0); dly();
    return nack;
}

u8 i2c_rbyte(int ack)
{
    u8 b = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
        dly(); scl_high();
        b = (b << 1) | sda_in();
        scl(0);
    }
    sda(!ack); dly();
    scl_high(); scl(0);
    sda(1);
    return b;
}

int i2c_probe(u8 addr7)
{
    i2c_start();
    int nack = i2c_wbyte((u8)(addr7 << 1));
    i2c_stop();
    return nack;
}
