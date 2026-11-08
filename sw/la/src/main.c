// Логический анализатор Antminer S9: lwIP (DHCP) + TCP-сервер на порту 5555.
// Основано на шаблоне Xilinx «lwIP Echo Server» (platform_zynq.c из шаблона).

#include <stdio.h>
#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "netif/xadapter.h"
#include "platform.h"
#include "platform_config.h"
#include "xil_printf.h"
#include "xparameters.h"

#include "la_server.h"
#include "probe.h"
#include "services.h"
#include "s9la_regs.h"

void tcp_fasttmr(void);
void tcp_slowtmr(void);

extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;
extern volatile int dhcp_timoutcntr;

static struct netif server_netif;
struct netif *echo_netif;   // имя требует platform_zynq.c из шаблона

static void print_ip(const char *msg, const ip_addr_t *ip)
{
    xil_printf("%s%d.%d.%d.%d\r\n", msg, ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));
}

// MAC из Device DNA кристалла: у каждой платы свой, локально администрируемый (02:...)
static void make_mac(u8 mac[6])
{
    for (int i = 0; i < 1000 && !(la_rd(LA_DNA_HI) & 0x80000000u); i++)
        ;
    u32 lo = la_rd(LA_DNA_LO), hi = la_rd(LA_DNA_HI);
    if (!(hi & 0x80000000u) || (lo == 0 && (hi & 0x01FFFFFFu) == 0)) {
        lo = 0x4C410001; hi = 0x53;   // DNA недоступен — фиксированный адрес
    }
    // перемешиваем 57 бит DNA в 40 бит
    u32 h1 = lo ^ (hi << 7) ^ (hi >> 13);
    u8  h2 = (u8)(hi ^ (hi >> 8) ^ (lo >> 24));
    mac[0] = 0x02;
    mac[1] = h1 >> 24; mac[2] = h1 >> 16; mac[3] = h1 >> 8; mac[4] = h1;
    mac[5] = h2;
    xil_printf("DNA %07x%08x → MAC %02x:%02x:%02x:%02x:%02x:%02x\r\n",
               hi & 0x01FFFFFFu, lo, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int main(void)
{
    ip_addr_t ipaddr, netmask, gw;
    u8 mac[6];

    echo_netif = &server_netif;
    init_platform();

    xil_printf("\r\n----- Antminer S9 logic analyzer -----\r\n");
    if (la_rd(LA_ID) != LA_ID_VALUE) {
        xil_printf("Ядро s9la_core не найдено (ID %08x) — битстрим не тот?\r\n", la_rd(LA_ID));
        return -1;
    }
    la_wr(LA_LED, 0);
    make_mac(mac);

    ipaddr.addr = 0; gw.addr = 0; netmask.addr = 0;
    lwip_init();
    if (!xemac_add(echo_netif, &ipaddr, &netmask, &gw, mac, PLATFORM_EMAC_BASEADDR)) {
        xil_printf("Ошибка инициализации Ethernet\r\n");
        return -1;
    }
    netif_set_default(echo_netif);
    platform_enable_interrupts();
    netif_set_up(echo_netif);

    dhcp_start(echo_netif);
    dhcp_timoutcntr = 24;
    while (echo_netif->ip_addr.addr == 0 && dhcp_timoutcntr > 0)
        xemacif_input(echo_netif);
    if (echo_netif->ip_addr.addr == 0) {
        xil_printf("DHCP не ответил, статический адрес 192.168.1.10\r\n");
        IP4_ADDR(&echo_netif->ip_addr, 192, 168, 1, 10);
        IP4_ADDR(&echo_netif->netmask, 255, 255, 255, 0);
        IP4_ADDR(&echo_netif->gw,      192, 168, 1, 1);
    }
    print_ip("IP:      ", &echo_netif->ip_addr);
    print_ip("Маска:   ", &echo_netif->netmask);
    print_ip("Шлюз:    ", &echo_netif->gw);

    la_server_init();
    probe_init();
    svc_xvc_init();
    svc_dap_init();
    svc_serprog_init();
    svc_uart_init();
    svc_cmd_init();
    svc_bridge_init();
    la_set_led(LED_NET, 1);

    for (;;) {
        if (TcpFastTmrFlag) { tcp_fasttmr(); TcpFastTmrFlag = 0; }
        if (TcpSlowTmrFlag) { tcp_slowtmr(); TcpSlowTmrFlag = 0; }
        xemacif_input(echo_netif);
        la_server_poll();
        svc_xvc_poll();
        svc_dap_poll();
        svc_serprog_poll();
        svc_uart_poll();
        svc_cmd_poll();
        svc_bridge_poll();
    }
    return 0;
}
