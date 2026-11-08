#include "tcpsrv.h"

#include <string.h>

#include "xil_printf.h"

static void drop(tcpsrv_t *s)
{
    s->pcb = NULL;
    s->rxlen = 0;
    if (s->closed) s->closed(s);
}

void srv_close(tcpsrv_t *s)
{
    struct tcp_pcb *pcb = s->pcb;
    if (!pcb) return;
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    xil_printf("%s: клиент отключён\r\n", s->name);
    drop(s);
}

static void on_err(void *arg, err_t err)
{
    tcpsrv_t *s = arg;
    if (!s) return;
    xil_printf("%s: ошибка соединения %d\r\n", s->name, err);
    drop(s);
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    tcpsrv_t *s = arg;
    (void)pcb;
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        srv_close(s);
        return ERR_OK;
    }
    if (s->rxlen + p->tot_len > SRV_RXBUF)
        return ERR_MEM;     // не должно случаться: окно меньше буфера
    pbuf_copy_partial(p, s->rx + s->rxlen, p->tot_len, 0);
    s->rxlen += p->tot_len;
    pbuf_free(p);
    if (s->process) s->process(s);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    tcpsrv_t *s = arg;
    (void)pcb; (void)len;
    if (s && s->pcb && s->process) s->process(s);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    tcpsrv_t *s = arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    if (s->pcb) {
        struct tcp_pcb *old = s->pcb;
        tcp_arg(old, NULL); tcp_recv(old, NULL); tcp_sent(old, NULL); tcp_err(old, NULL);
        tcp_abort(old);
        xil_printf("%s: новый клиент вытесняет старого\r\n", s->name);
        drop(s);
    }
    s->pcb = pcb;
    s->rxlen = 0;
    tcp_arg(pcb, s);
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    xil_printf("%s: клиент подключён\r\n", s->name);
    if (s->connected) s->connected(s);
    return ERR_OK;
}

void srv_start(tcpsrv_t *s)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, s->port) != ERR_OK) {
        xil_printf("%s: не удалось занять порт %d\r\n", s->name, s->port);
        return;
    }
    pcb = tcp_listen_with_backlog(pcb, 1);
    tcp_arg(pcb, s);
    tcp_accept(pcb, on_accept);
    xil_printf("%s: TCP %d\r\n", s->name, s->port);
}

void srv_poll(tcpsrv_t *s)
{
    if (s->pcb && s->rxlen && s->process) s->process(s);
}

u32 srv_space(tcpsrv_t *s)
{
    if (!s->pcb) return 0;
    if (tcp_sndqueuelen(s->pcb) > TCP_SND_QUEUELEN - 8) return 0;
    return tcp_sndbuf(s->pcb);
}

int srv_send(tcpsrv_t *s, const void *d, u32 len)
{
    const u8 *p = d;
    if (!s->pcb) return -1;
    if (len > srv_space(s)) return -1;
    while (len) {
        u16 n = len > 0x8000 ? 0x8000 : (u16)len;
        if (tcp_write(s->pcb, p, n, TCP_WRITE_FLAG_COPY) != ERR_OK) return -1;
        p += n; len -= n;
    }
    return 0;
}

void srv_flush(tcpsrv_t *s)
{
    if (s->pcb) tcp_output(s->pcb);
}

void srv_consume(tcpsrv_t *s, u32 n)
{
    if (n > s->rxlen) n = s->rxlen;
    memmove(s->rx, s->rx + n, s->rxlen - n);
    s->rxlen -= n;
    if (s->pcb) {
        while (n) {
            u16 k = n > 0xFFFF ? 0xFFFF : (u16)n;
            tcp_recved(s->pcb, k);
            n -= k;
        }
    }
}
