// TCP-сервер логического анализатора (протокол — sw/la/PROTOCOL.md), lwIP raw API.
//
// Выборки отправляются прямо из кольцевого буфера в DDR без копирования
// (tcp_write без TCP_WRITE_FLAG_COPY): буфер не перезаписывается, пока не
// подтверждены все отправленные байты (новый START ждёт).

#include "la_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/tcp.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "s9la_regs.h"

#define CHUNK      16384u      // данных на один кадр D
#define CMD_LINE_MAX   512

typedef enum { S_IDLE, S_CAPTURE, S_SEND } state_t;

static struct {
    struct tcp_pcb *pcb;
    char     line[CMD_LINE_MAX];
    int      line_len;

    // настройки захвата
    u32      div, unit, samples, pre, tmask, tval, trise, tfall, pattern;

    // текущий захват
    state_t  st;
    int      start_pending;
    u32      first, n, sent;   // в выборках
    int      trigger;
    int      abort;
    const char *endmsg;
    u32      pend_off, pend_len;  // заголовок кадра ушёл, данные ещё нет

    // учёт байт, отданных lwIP без копирования
    u32      written, acked;
    u32      led;
} s;

void la_set_led(unsigned bit, int on)
{
    if (on) s.led |= bit; else s.led &= ~bit;
    la_wr(LA_LED, s.led);
}

static u32 ctrl_level(void)
{
    return (s.pattern == 1 ? LA_CTRL_TEST : 0) | (s.pattern == 2 ? LA_CTRL_TXGEN : 0);
}

static err_t write_counted(const void *p, u16 len, u8 flags)
{
    err_t e = tcp_write(s.pcb, p, len, flags);
    if (e == ERR_OK) s.written += len;
    return e;
}

static void send_frame(char type, const void *data, u32 len)
{
    u8 hdr[8] = {'S', '9', (u8)type, 0, len, len >> 8, len >> 16, len >> 24};
    if (!s.pcb) return;
    if (write_counted(hdr, 8, TCP_WRITE_FLAG_COPY) != ERR_OK ||
        (len && write_counted(data, len, TCP_WRITE_FLAG_COPY) != ERR_OK))
        xil_printf("la: не удалось отправить кадр %c\r\n", type);
    tcp_output(s.pcb);
}

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    send_frame('R', buf, n > 0 ? (u32)n : 0);
}

// ---------------------------------------------------------------------
// Команды
// ---------------------------------------------------------------------
static void cmd_conf(char *args)
{
    u32 rate = LA_CLOCK_HZ / (s.div + 1);
    u32 unit = s.unit, samples = s.samples, pre = s.pre;
    u32 tmask = s.tmask, tval = s.tval, trise = s.trise, tfall = s.tfall, pattern = s.pattern;

    if (s.st != S_IDLE || s.start_pending) { reply("ERR busy"); return; }

    for (char *tok = strtok(args, " \t"); tok; tok = strtok(NULL, " \t")) {
        char *eq = strchr(tok, '=');
        if (!eq) { reply("ERR bad token %s", tok); return; }
        *eq++ = 0;
        if      (!strcmp(tok, "rate"))    rate    = strtoul(eq, NULL, 10);
        else if (!strcmp(tok, "unit"))    unit    = strtoul(eq, NULL, 10);
        else if (!strcmp(tok, "samples")) samples = strtoul(eq, NULL, 10);
        else if (!strcmp(tok, "pre"))     pre     = strtoul(eq, NULL, 10);
        else if (!strcmp(tok, "tmask"))   tmask   = strtoul(eq, NULL, 16);
        else if (!strcmp(tok, "tval"))    tval    = strtoul(eq, NULL, 16);
        else if (!strcmp(tok, "trise"))   trise   = strtoul(eq, NULL, 16);
        else if (!strcmp(tok, "tfall"))   tfall   = strtoul(eq, NULL, 16);
        else if (!strcmp(tok, "pattern")) pattern = strtoul(eq, NULL, 10);
        else { reply("ERR unknown key %s", tok); return; }
    }

    if (unit != 1 && unit != 2 && unit != 4) { reply("ERR unit"); return; }
    if (rate == 0 || rate > LA_CLOCK_HZ)      { reply("ERR rate"); return; }
    if (samples == 0 || (u64)samples * unit > RING_SIZE) {
        reply("ERR samples (max %u)", (unsigned)(RING_SIZE / unit)); return;
    }
    if (pre >= samples) { reply("ERR pre"); return; }
    if (pattern > 2)    { reply("ERR pattern"); return; }

    u32 div = (LA_CLOCK_HZ + rate / 2) / rate;
    if (div < 1) div = 1;
    s.div = div - 1;
    s.unit = unit; s.samples = samples; s.pre = pre;
    s.tmask = tmask; s.tval = tval; s.trise = trise; s.tfall = tfall; s.pattern = pattern;

    la_wr(LA_CTRL, ctrl_level());
    reply("OK rate=%u samples=%u", (unsigned)(LA_CLOCK_HZ / div), (unsigned)samples);
}

static void do_start(void)
{
    s.start_pending = 0;
    la_wr(LA_DIV,   s.div);
    la_wr(LA_UNIT,  s.unit == 1 ? 0 : s.unit == 2 ? 1 : 2);
    la_wr(LA_RBASE, RING_BASE);
    la_wr(LA_RSIZE, RING_SIZE);
    la_wr(LA_PRE,   s.pre);
    la_wr(LA_POST,  s.samples - s.pre);
    la_wr(LA_TMASK, s.tmask);
    la_wr(LA_TVAL,  s.tval);
    la_wr(LA_TRISE, s.trise);
    la_wr(LA_TFALL, s.tfall);
    la_wr(LA_CTRL,  ctrl_level() | LA_CTRL_START);
    s.st = S_CAPTURE;
    s.abort = 0;
    xil_printf("la: захват: %u выборок по %u байт, div %u, pre %u\r\n",
               s.samples, s.unit, s.div, s.pre);
}

static void cmd_start(void)
{
    if (s.st != S_IDLE || s.start_pending) { reply("ERR busy"); return; }
    reply("OK");
    // буфер можно перезаписывать только когда всё отправленное подтверждено
    s.start_pending = 1;
}

static void cmd_stop(void)
{
    if (s.start_pending) {
        // захват ещё не начался — сразу пустой результат
        s.start_pending = 0;
        send_frame('H', "samples=0 trigger=-1", 20);
        send_frame('E', "OK aborted", 10);
    } else if (s.st == S_CAPTURE) {
        la_wr(LA_CTRL, ctrl_level() | LA_CTRL_STOP);
    } else if (s.st == S_SEND) {
        s.abort = 1;
    }
}

static void handle_line(char *line)
{
    char *args = strchr(line, ' ');
    if (args) *args++ = 0; else args = line + strlen(line);

    if (!strcmp(line, "ID")) {
        reply("S9LA 1 channels=%u clock=%u memory=%u", LA_CHANNELS, LA_CLOCK_HZ, RING_SIZE);
    } else if (!strcmp(line, "CONF")) {
        cmd_conf(args);
    } else if (!strcmp(line, "START")) {
        cmd_start();
    } else if (!strcmp(line, "STOP")) {
        cmd_stop();
    } else if (!strcmp(line, "INPUT")) {
        reply("OK %08x", (unsigned)la_rd(LA_INPUT));
    } else if (!strcmp(line, "PING")) {
        reply("OK");
    } else if (line[0]) {
        reply("ERR unknown command %s", line);
    }
}

// ---------------------------------------------------------------------
// Выгрузка
// ---------------------------------------------------------------------
static void prepare_send(void)
{
    u32 status = la_rd(LA_STATUS);
    u32 count  = la_rd(LA_COUNT);
    u32 cap    = RING_SIZE / s.unit;

    s.endmsg = (status & LA_ST_OVERFLOW) ? "ERR overflow" :
               (status & LA_ST_AXIERR)   ? "ERR axi" : "OK";

    if (status & LA_ST_TRIGGERED) {
        u32 tp = la_rd(LA_TRIGPOS);
        s.first   = tp - s.pre;
        s.n       = count - s.first;
        if (s.n > s.samples) s.n = s.samples;
        s.trigger = s.pre;
    } else {
        s.n = count;
        if (s.n > s.samples) s.n = s.samples;
        if (s.n > cap)       s.n = cap;
        s.first   = count - s.n;
        s.trigger = -1;
    }
    s.sent = 0;
    s.pend_len = 0;

    char h[100];
    int len = snprintf(h, sizeof h, "samples=%u trigger=%d unit=%u rate=%u",
                       (unsigned)s.n, s.trigger, (unsigned)s.unit,
                       (unsigned)(LA_CLOCK_HZ / (s.div + 1)));
    send_frame('H', h, len);
    xil_printf("la: захвачено %u, отправляю %u (триггер %d) %s\r\n",
               count, s.n, s.trigger, s.endmsg);
    la_set_led(LED_SEND, 1);
    s.st = S_SEND;
}

static void finish_send(void)
{
    const char *m = s.abort ? "OK aborted" : s.endmsg;
    send_frame('E', m, strlen(m));
    s.st = S_IDLE;
    la_set_led(LED_SEND, 0);
}

static void push_data(void)
{
    if (!s.pcb) { s.st = S_IDLE; la_set_led(LED_SEND, 0); return; }

    for (;;) {
        if (s.pend_len) {
            // заголовок уже в очереди, досылаем данные кадра
            if (tcp_sndbuf(s.pcb) < s.pend_len) break;
            void *p = (void *)(RING_BASE + s.pend_off);
            Xil_DCacheInvalidateRange((INTPTR)p, s.pend_len);
            if (write_counted(p, s.pend_len, 0) != ERR_OK) break;
            s.sent += s.pend_len / s.unit;
            s.pend_len = 0;
            continue;
        }
        if (s.abort || s.sent >= s.n) {
            finish_send();
            break;
        }
        u32 avail = tcp_sndbuf(s.pcb);
        if (avail < 8 + 2048 || tcp_sndqueuelen(s.pcb) > TCP_SND_QUEUELEN - 16) break;

        u32 left  = (s.n - s.sent) * s.unit;
        u32 chunk = left < CHUNK ? left : CHUNK;
        if (chunk > avail - 8) chunk = (avail - 8) & ~3u;
        u32 off   = (u32)(((u64)(s.first + s.sent) * s.unit) % RING_SIZE);
        if (chunk > RING_SIZE - off) chunk = RING_SIZE - off;

        u8 hdr[8] = {'S', '9', 'D', 0, chunk, chunk >> 8, chunk >> 16, chunk >> 24};
        if (write_counted(hdr, 8, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE) != ERR_OK) break;
        s.pend_off = off;
        s.pend_len = chunk;
    }
    tcp_output(s.pcb);
}

// ---------------------------------------------------------------------
// lwIP
// ---------------------------------------------------------------------
static void conn_gone(void)
{
    s.pcb = NULL;
    s.line_len = 0;
    s.start_pending = 0;
    s.written = s.acked = 0;
    if (s.st == S_CAPTURE) la_wr(LA_CTRL, ctrl_level() | LA_CTRL_STOP);
    if (s.st == S_SEND) { s.st = S_IDLE; la_set_led(LED_SEND, 0); }
    la_set_led(LED_CLIENT, 0);
}

static void on_err(void *arg, err_t err)
{
    (void)arg;
    xil_printf("la: ошибка соединения %d\r\n", err);
    conn_gone();
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg; (void)pcb;
    s.acked += len;
    if (s.st == S_SEND) push_data();
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL);
        tcp_close(pcb);
        xil_printf("la: клиент отключился\r\n");
        conn_gone();
        return ERR_OK;
    }
    for (struct pbuf *q = p; q; q = q->next) {
        const char *d = q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            char c = d[i];
            if (c == '\n' || c == '\r') {
                if (s.line_len) {
                    s.line[s.line_len] = 0;
                    handle_line(s.line);
                    s.line_len = 0;
                }
            } else if (s.line_len < CMD_LINE_MAX - 1) {
                s.line[s.line_len++] = c;
            }
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    if (s.pcb) {
        xil_printf("la: второй клиент отклонён\r\n");
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    s.pcb = pcb;
    s.line_len = 0;
    s.written = s.acked = 0;
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    la_set_led(LED_CLIENT, 1);
    xil_printf("la: клиент подключён\r\n");
    return ERR_OK;
}

void la_server_init(void)
{
    memset(&s, 0, sizeof s);
    s.div = 0; s.unit = 1; s.samples = 1000000; s.pre = 0; s.pattern = 0;
    la_wr(LA_CTRL, 0);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, LA_TCP_PORT) != ERR_OK) {
        xil_printf("la: не удалось занять порт %d\r\n", LA_TCP_PORT);
        return;
    }
    pcb = tcp_listen_with_backlog(pcb, 1);
    tcp_accept(pcb, on_accept);
    xil_printf("la: слушаю TCP %d\r\n", LA_TCP_PORT);
}

void la_server_poll(void)
{
    if (s.start_pending && s.acked == s.written) {
        do_start();
    } else if (s.st == S_CAPTURE) {
        if ((la_rd(LA_STATUS) & LA_ST_MASK) == LA_ST_DONE) {
            if (s.pcb) prepare_send();
            else { s.st = S_IDLE; }
        }
    } else if (s.st == S_SEND) {
        push_data();
    }
}
