// Простой TCP-сервер на lwIP raw API: один клиент (новый вытесняет старого),
// входной буфер с управлением окном (tcp_recved — только когда данные разобраны).
#pragma once
#include "lwip/tcp.h"
#include "xil_types.h"

#define SRV_RXBUF (20 * 1024)   // больше TCP_WND — клиент не переполнит буфер

typedef struct tcpsrv tcpsrv_t;
struct tcpsrv {
    const char      *name;
    u16              port;
    struct tcp_pcb  *pcb;                 // текущий клиент (NULL — нет)
    u8               rx[SRV_RXBUF];
    u32              rxlen;
    void (*process)(tcpsrv_t *s);         // разобрать rx (вызывается при приёме, отправке и в poll)
    void (*connected)(tcpsrv_t *s);
    void (*closed)(tcpsrv_t *s);
};

void srv_start(tcpsrv_t *s);
void srv_poll(tcpsrv_t *s);
void srv_close(tcpsrv_t *s);
u32  srv_space(tcpsrv_t *s);                          // свободно в буфере отправки
int  srv_send(tcpsrv_t *s, const void *d, u32 len);   // 0 — поставлено в очередь
void srv_flush(tcpsrv_t *s);
void srv_consume(tcpsrv_t *s, u32 n);                 // убрать n байт из rx

static inline u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static inline u32 le24(const u8 *p) { return p[0] | (p[1] << 8) | ((u32)p[2] << 16); }
static inline u16 le16(const u8 *p) { return p[0] | (p[1] << 8); }
static inline void put_le32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static inline void put_le16(u8 *p, u16 v) { p[0] = v; p[1] = v >> 8; }
