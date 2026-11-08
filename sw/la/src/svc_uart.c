// UART через линии FAN2 (TX) и FAN3 (RX):
//   TCP 5558 — RFC2217 (telnet + COM-PORT-OPTION: скорость задаёт клиент),
//              python -m serial.tools.miniterm rfc2217://<адрес>:5558 115200
//   TCP 5559 — сырой поток на текущей скорости: socat - TCP:<адрес>:5559
// Формат кадра — 8N1 (другие запросы RFC2217 подтверждаются как 8N1).
#include <string.h>

#include "probe.h"
#include "services.h"
#include "tcpsrv.h"
#include "xil_printf.h"

#define IAC   255
#define SB    250
#define SE    240
#define WILL  251
#define WONT  252
#define DO    253
#define DONT  254
#define OPT_BINARY 0
#define OPT_SGA    3
#define OPT_COMPORT 44

static tcpsrv_t s_rfc, s_raw;
static u32 baud = 115200;
static int users;

// состояние разбора telnet (только для RFC2217)
static int tn_state;          // 0 данные, 1 после IAC, 2 после WILL/WONT/DO/DONT, 3 в SB, 4 IAC в SB
static u8  tn_cmd;
static u8  sb[16];
static u32 sb_len;

static void tn_send3(u8 a, u8 b, u8 c)
{
    u8 r[3] = {a, b, c};
    srv_send(&s_rfc, r, 3);
}

static void comport_reply(u8 code, const u8 *d, u32 n)
{
    u8 r[16];
    u32 k = 0;
    r[k++] = IAC; r[k++] = SB; r[k++] = OPT_COMPORT; r[k++] = code + 100;
    for (u32 i = 0; i < n; i++) {
        r[k++] = d[i];
        if (d[i] == IAC) r[k++] = IAC;
    }
    r[k++] = IAC; r[k++] = SE;
    srv_send(&s_rfc, r, k);
}

static void comport(void)
{
    if (sb_len < 2 || sb[0] != OPT_COMPORT) return;
    u8 cmd = sb[1];
    const u8 *d = sb + 2;
    u32 n = sb_len - 2;
    u8 v[4];
    switch (cmd) {
    case 0:     // SIGNATURE
        comport_reply(0, (const u8 *)"S9probe", 7);
        break;
    case 1: {   // SET-BAUDRATE (BE), 0 — запрос
        if (n >= 4) {
            u32 b = (u32)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
            if (b) baud = probe_uart_baud(b);
        }
        v[0] = baud >> 24; v[1] = baud >> 16; v[2] = baud >> 8; v[3] = baud;
        comport_reply(1, v, 4);
        break;
    }
    case 2: v[0] = 8; comport_reply(2, v, 1); break;     // DATASIZE: 8
    case 3: v[0] = 1; comport_reply(3, v, 1); break;     // PARITY: NONE
    case 4: v[0] = 1; comport_reply(4, v, 1); break;     // STOPSIZE: 1
    case 5:     // SET-CONTROL: подтверждаем как есть
    case 10:    // SET-LINESTATE-MASK
    case 11:    // SET-MODEMSTATE-MASK
    case 12:    // PURGE-DATA
        comport_reply(cmd, d, n > 1 ? 1 : n);
        break;
    default:
        break;
    }
}

static void telnet_opt(u8 cmd, u8 opt)
{
    int ok = (opt == OPT_BINARY || opt == OPT_SGA || opt == OPT_COMPORT);
    if (cmd == WILL) tn_send3(IAC, ok ? DO : DONT, opt);
    else if (cmd == DO) tn_send3(IAC, ok ? WILL : WONT, opt);
}

static void rfc_process(tcpsrv_t *s)
{
    u32 i = 0;
    while (i < s->rxlen && probe_uart_tx_free() > 0 && srv_space(s) > 64) {
        u8 c = s->rx[i++];
        switch (tn_state) {
        case 0:
            if (c == IAC) tn_state = 1;
            else probe_uart_put(c);
            break;
        case 1:
            if (c == IAC) { probe_uart_put(IAC); tn_state = 0; }
            else if (c == SB) { tn_state = 3; sb_len = 0; }
            else if (c >= WILL) { tn_cmd = c; tn_state = 2; }
            else tn_state = 0;
            break;
        case 2:
            telnet_opt(tn_cmd, c);
            tn_state = 0;
            break;
        case 3:
            if (c == IAC) tn_state = 4;
            else if (sb_len < sizeof sb) sb[sb_len++] = c;
            break;
        case 4:
            if (c == SE) { comport(); tn_state = 0; }
            else { if (sb_len < sizeof sb) sb[sb_len++] = c; tn_state = 3; }
            break;
        }
    }
    srv_consume(s, i);
    srv_flush(s);
}

static void raw_process(tcpsrv_t *s)
{
    u32 i = 0;
    while (i < s->rxlen && probe_uart_tx_free() > 0)
        probe_uart_put(s->rx[i++]);
    srv_consume(s, i);
}

static void on_connect(tcpsrv_t *s)
{
    (void)s;
    if (users++ == 0) probe_uart_enable(1);
    tn_state = 0;
}

static void on_close(tcpsrv_t *s)
{
    (void)s;
    if (--users <= 0) { users = 0; probe_uart_enable(0); }
}

void svc_uart_init(void)
{
    baud = probe_uart_baud(115200);
    s_rfc.name = "uart-rfc2217"; s_rfc.port = 5558; s_rfc.process = rfc_process;
    s_rfc.connected = on_connect; s_rfc.closed = on_close;
    s_raw.name = "uart-raw";     s_raw.port = 5559; s_raw.process = raw_process;
    s_raw.connected = on_connect; s_raw.closed = on_close;
    srv_start(&s_rfc);
    srv_start(&s_raw);
}

void svc_uart_poll(void)
{
    srv_poll(&s_rfc);
    srv_poll(&s_raw);

    // UART → клиенты
    tcpsrv_t *dst = s_rfc.pcb ? &s_rfc : (s_raw.pcb ? &s_raw : NULL);
    if (!dst) {
        while (probe_uart_get() >= 0)
            ;
        return;
    }
    u8 b[128];
    u32 n = 0;
    int c;
    while (n < sizeof b - 2 && srv_space(dst) > sizeof b && (c = probe_uart_get()) >= 0) {
        b[n++] = (u8)c;
        if (dst == &s_rfc && c == IAC) b[n++] = IAC;
    }
    if (n) {
        srv_send(dst, b, n);
        srv_flush(dst);
    }
}
