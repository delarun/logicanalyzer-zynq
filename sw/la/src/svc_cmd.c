// Текстовая консоль отладочных интерфейсов, TCP 5560 (nc <адрес> 5560):
//   help | pins | reset <0|1|pulse [мс]> | gpio <линия> <0|1|z> | jtag scan [кГц]
//   i2c scan | i2c r <адр> <n> | i2c w <адр> <hex...> | i2c wr <адр> <hex...> <n> | i2c speed <Гц>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i2c.h"
#include "probe.h"
#include "services.h"
#include "tcpsrv.h"

static tcpsrv_t srv;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) srv_send(&srv, b, (u32)n < sizeof b ? (u32)n : sizeof b - 1);
}

static int parse_hex(char **tok, u8 *d, int max)
{
    int n = 0;
    char *t;
    while (n < max && (t = strtok(NULL, " \t")) && strlen(t) > 0) {
        char *e;
        long v = strtol(t, &e, 16);
        if (*e) { *tok = t; return n; }
        d[n++] = (u8)v;
    }
    *tok = t;
    return n;
}

static void cmd_i2c(void)
{
    char *sub = strtok(NULL, " \t");
    if (!sub) { out("ERR i2c scan|r|w|wr|speed\n"); return; }
    if (!strcmp(sub, "speed")) {
        char *a = strtok(NULL, " \t");
        u32 hz = i2c_speed(a ? strtoul(a, NULL, 0) : 100000);
        out("OK %u Гц\n", (unsigned)hz);
        return;
    }
    if (!strcmp(sub, "scan")) {
        out("    ");
        for (int i = 0; i < 16; i++) out(" %x ", i);
        for (int a = 0; a < 128; a++) {
            if (a % 16 == 0) out("\n%02x: ", a);
            if (a < 0x08 || a > 0x77) { out("   "); continue; }
            if (i2c_probe((u8)a)) out("-- "); else out("%02x ", a);
        }
        out("\n");
        return;
    }
    char *as = strtok(NULL, " \t");
    if (!as) { out("ERR нет адреса\n"); return; }
    u8 addr = (u8)strtoul(as, NULL, 16);
    u8 w[64], r[64];
    int nw = 0, nr = 0;
    char *rest = NULL;
    if (!strcmp(sub, "r")) {
        char *n = strtok(NULL, " \t");
        nr = n ? atoi(n) : 1;
    } else if (!strcmp(sub, "w")) {
        nw = parse_hex(&rest, w, sizeof w);
    } else if (!strcmp(sub, "wr")) {
        nw = parse_hex(&rest, w, sizeof w);
        nr = rest ? atoi(rest) : 1;
    } else {
        out("ERR i2c scan|r|w|wr|speed\n");
        return;
    }
    if (nr > (int)sizeof r) nr = sizeof r;
    int err = 0;
    if (nw) {
        i2c_start();
        err |= i2c_wbyte(addr << 1);
        for (int i = 0; i < nw && !err; i++) err |= i2c_wbyte(w[i]);
    }
    if (!err && nr) {
        i2c_start();       // повторный старт
        err |= i2c_wbyte((addr << 1) | 1);
        for (int i = 0; i < nr && !err; i++) r[i] = i2c_rbyte(i < nr - 1);
    }
    i2c_stop();
    if (err) { out("NACK\n"); return; }
    out("OK");
    for (int i = 0; i < nr; i++) out(" %02x", r[i]);
    out("\n");
}

// ---------------- JTAG: IDCODE всех устройств цепочки ----------------
static void cmd_jtag(void)
{
    char *sub = strtok(NULL, " \t");
    if (!sub || strcmp(sub, "scan")) { out("ERR jtag scan [кГц]\n"); return; }
    char *k = strtok(NULL, " \t");
    u32 hz = k ? strtoul(k, NULL, 0) * 1000 : 1000000;
    probe_mode(PM_JTAG, hz);
    // Test-Logic-Reset → Run-Test/Idle → Shift-DR
    u8 nav = 0x5F;   // TMS: 1 1 1 1 1 0 1 0 0 (LSB первым) = 0b0_0101_1111, 9 бит
    u8 tms[2] = {nav, 0x00}, tdi[2] = {0, 0};
    probe_shift(PM_JTAG, 9, tms, tdi, NULL, NULL);
    u8 t0[32], ones[32], tdo[32];
    memset(t0, 0, sizeof t0);
    memset(ones, 0xFF, sizeof ones);
    t0[31] = 0x80;     // последний бит — выход из Shift-DR
    probe_shift(PM_JTAG, 256, t0, ones, NULL, tdo);
    u8 back[1] = {0x01};   // Exit1 → Update-DR → (TMS 1,0)
    probe_shift(PM_JTAG, 2, back, NULL, NULL, NULL);
    probe_mode(PM_NONE, 0);

    int found = 0, bit = 0, all1 = 1, all0 = 1;
    for (int i = 0; i < 32; i++) { if (tdo[i] != 0xFF) all1 = 0; if (tdo[i]) all0 = 0; }
    if (all1) { out("TDO всё время 1: нет цепочки (не подключено или нет питания цели)\n"); return; }
    if (all0) { out("TDO всё время 0: линия прижата к земле\n"); return; }
    while (bit + 32 <= 256) {
        if (!((tdo[bit / 8] >> (bit % 8)) & 1)) { out("  устройство %d: BYPASS\n", found++); bit++; continue; }
        u32 id = 0;
        for (int i = 0; i < 32; i++) id |= (u32)((tdo[(bit + i) / 8] >> ((bit + i) % 8)) & 1) << i;
        if (id == 0xFFFFFFFFu) break;
        out("  устройство %d: IDCODE 0x%08X\n", found++, (unsigned)id);
        bit += 32;
    }
    out("OK устройств: %d (TCK %u Гц)\n", found, (unsigned)probe_freq());
}

static const char *pin_names[PIN_COUNT] = {
    "J9_TX/TCK", "J9_RST/TMS", "J9_PLUG/TDI", "J9_RX/TDO", "FAN1/nRESET", "FAN2/UART_TX",
    "FAN3/UART_RX", "FAN4/SCK", "FAN5/MOSI", "FAN6/MISO", "SCL1/CS", "SCL2/I2C_SCL",
    "SDA2/I2C_SDA", "SDA1/AUX",
};

static void handle(char *line)
{
    char *cmd = strtok(line, " \t");
    if (!cmd) return;
    if (!strcmp(cmd, "help")) {
        out("pins | reset <0|1|pulse [мс]> | gpio <0..13> <0|1|z> | jtag scan [кГц]\n"
            "i2c scan | i2c r <адр> <n> | i2c w <адр> <hex..> | i2c wr <адр> <hex..> <n> | i2c speed <Гц>\n");
    } else if (!strcmp(cmd, "pins")) {
        u32 in = probe_gpio_in();
        for (int i = 0; i < PIN_COUNT; i++) out("%2d %-13s %u\n", i, pin_names[i], (unsigned)((in >> i) & 1));
    } else if (!strcmp(cmd, "reset")) {
        char *a = strtok(NULL, " \t");
        if (a && !strcmp(a, "pulse")) {
            char *ms = strtok(NULL, " \t");
            probe_od(PIN_NRST, 0);
            probe_delay_us((ms ? atoi(ms) : 100) * 1000);
            probe_od(PIN_NRST, 1);
        } else {
            probe_od(PIN_NRST, a ? atoi(a) : 1);
        }
        out("OK\n");
    } else if (!strcmp(cmd, "gpio")) {
        char *p = strtok(NULL, " \t"), *v = strtok(NULL, " \t");
        if (!p || !v) { out("ERR gpio <линия> <0|1|z>\n"); return; }
        int pin = atoi(p);
        if (pin < 0 || pin >= PIN_COUNT) { out("ERR линия 0..13\n"); return; }
        u32 m = 1u << pin;
        if (v[0] == 'z') probe_gpio(m, 0, 0);
        else probe_gpio(m, atoi(v) ? m : 0, m);
        out("OK\n");
    } else if (!strcmp(cmd, "i2c")) {
        cmd_i2c();
    } else if (!strcmp(cmd, "jtag")) {
        cmd_jtag();
    } else {
        out("ERR неизвестная команда, help\n");
    }
}

static void process(tcpsrv_t *s)
{
    for (;;) {
        u8 *nl = memchr(s->rx, '\n', s->rxlen);
        if (!nl) {
            if (s->rxlen > 512) srv_consume(s, s->rxlen);
            break;
        }
        if (srv_space(s) < 4096) break;
        u32 n = nl - s->rx;
        char line[520];
        if (n > sizeof line - 1) n = sizeof line - 1;
        memcpy(line, s->rx, n);
        line[n] = 0;
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        srv_consume(s, (nl - s->rx) + 1);
        handle(line);
    }
    srv_flush(s);
}

static void connected(tcpsrv_t *s)
{
    (void)s;
    out("S9 probe: help — список команд\n");
    srv_flush(&srv);
}

void svc_cmd_init(void)
{
    srv.name = "cmd";
    srv.port = 5560;
    srv.process = process;
    srv.connected = connected;
    srv_start(&srv);
}

void svc_cmd_poll(void) { srv_poll(&srv); }
