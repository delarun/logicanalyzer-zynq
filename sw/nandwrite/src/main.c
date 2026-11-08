// Запись образа (BOOT.BIN) в NAND платы S9 — замена program_flash, который
// падает при работе через XVC.
//
// Хост (scripts/nand_write.tcl) по JTAG:
//   1. загружает эту программу и образ по адресу IMAGE_ADDR,
//   2. пишет длину образа в MB_LEN и 0 в MB_STATUS,
//   3. запускает программу и опрашивает MB_STATUS.
//
// Образ пишется в хорошие блоки подряд, начиная с блока 0, плохие пропускаются —
// так же читают BootROM и FSBL. Каждый блок проверяется чтением.

#include <string.h>

#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xnandps.h"
#include "xnandps_bbm.h"
#include "xparameters.h"

#define MB_BASE     0x01F00000u
#define MB_LEN      (*(volatile u32 *)(MB_BASE + 0x0))
#define MB_STATUS   (*(volatile u32 *)(MB_BASE + 0x4))   // 1 — идёт, 2 — готово, 0xEnnn — ошибка
#define MB_DONE     (*(volatile u32 *)(MB_BASE + 0x8))   // записано байт
#define MB_DIAG(i)  (*(volatile u32 *)(MB_BASE + 0x10 + 4 * (i)))   // диагностика
#define IMAGE_ADDR  0x02000000u

#define ST_RUNNING  1u
#define ST_OK       2u
#define ST_ERR(n)   (0xE000u | (n))

static XNandPs nand;

extern void XNandPs_SendCommand(XNandPs *InstancePtr, XNandPs_CommandFormat *Command,
                                int Page, int Column);

// READ ID (0x90) по адресу addr, n байт → упакованы в u32 (младший — первый)
static u32 read_id(u8 addr, int n)
{
    XNandPs_CommandFormat cmd = {0x90, XNANDPS_END_CMD_NONE, 1, XNANDPS_END_CMD_INVALID};
    u32 v = 0;
    XNandPs_SendCommand(&nand, &cmd, XNANDPS_PAGE_NOT_VALID, addr);
    for (int i = 0; i < n; i++)
        v |= (u32)Xil_In8(nand.DataPhaseAddr) << (8 * i);
    return v;
}
static u8 wbuf[256 * 1024] __attribute__((aligned(64)));
static u8 rbuf[256 * 1024] __attribute__((aligned(64)));

static void set_status(u32 st)
{
    MB_STATUS = st;
    Xil_DCacheFlushRange(MB_BASE, 32);
}

int main(void)
{
    u32 len = MB_LEN;
    const u8 *img = (const u8 *)IMAGE_ADDR;

    xil_printf("\r\n--- nandwrite: образ %u байт ---\r\n", (unsigned)len);
    set_status(ST_RUNNING);
    MB_DONE = 0;

    XNandPs_Config *cfg = XNandPs_LookupConfig(XPAR_XNANDPS_0_DEVICE_ID);
    int rc = cfg ? XNandPs_CfgInitialize(&nand, cfg, cfg->SmcBase, cfg->FlashBase) : -1;
    // диагностика: код возврата, геометрия (нули — ONFI не прочитался), ширина шины
    MB_DIAG(0) = (u32)rc;
    MB_DIAG(1) = nand.Geometry.BytesPerPage;
    MB_DIAG(2) = nand.Geometry.BlockSize;
    MB_DIAG(3) = nand.Geometry.NumBlocks;
    MB_DIAG(4) = nand.Geometry.FlashWidth;
    MB_DIAG(5) = nand.EccMode;
    if (rc != XST_SUCCESS && cfg) {
        MB_DIAG(6) = read_id(0x00, 4);    // производитель, устройство, 3-й, 4-й байт ID
        MB_DIAG(7) = read_id(0x00, 8) ;   // (повтор для проверки стабильности)
        MB_DIAG(8) = read_id(0x20, 4);    // "ONFI" у ONFI-совместимых
        xil_printf("NAND ID %08x, ID@20 %08x\r\n", (unsigned)MB_DIAG(6), (unsigned)MB_DIAG(8));
    }
    Xil_DCacheFlushRange(MB_BASE, 64);
    if (rc != XST_SUCCESS) {
        xil_printf("Ошибка инициализации NAND: %d, страница %u, блок %u\r\n", rc,
                   (unsigned)nand.Geometry.BytesPerPage, (unsigned)nand.Geometry.BlockSize);
        set_status(ST_ERR(1));
        return 1;
    }
    u32 bsize = nand.Geometry.BlockSize;
    u32 nblocks = nand.Geometry.NumBlocks;
    xil_printf("NAND: страница %u, блок %u, блоков %u, всего %u МБ\r\n",
               (unsigned)nand.Geometry.BytesPerPage, (unsigned)bsize, (unsigned)nblocks,
               (unsigned)(nand.Geometry.DeviceSize >> 20));

    if (len == 0 || len > 64u * 1024 * 1024 || bsize > sizeof(wbuf)) {
        xil_printf("Неверная длина образа или размер блока\r\n");
        set_status(ST_ERR(2));
        return 1;
    }

    u32 done = 0, block = 0, bad = 0;
    while (done < len) {
        if (block >= nblocks / 2) {     // образ загрузки — только в начале NAND
            xil_printf("Не хватило хороших блоков\r\n");
            set_status(ST_ERR(3));
            return 1;
        }
        if (XNandPs_IsBlockBad(&nand, block) == XST_SUCCESS) {
            xil_printf("блок %u плохой, пропускаю\r\n", (unsigned)block);
            block++; bad++;
            continue;
        }
        u32 n = len - done < bsize ? len - done : bsize;
        memset(wbuf, 0xFF, bsize);
        memcpy(wbuf, img + done, n);

        u64 off = (u64)block * bsize;
        int ok = XNandPs_EraseBlock(&nand, block) == XST_SUCCESS &&
                 XNandPs_Write(&nand, off, bsize, wbuf, NULL) == XST_SUCCESS &&
                 XNandPs_Read(&nand, off, bsize, rbuf, NULL) == XST_SUCCESS &&
                 memcmp(wbuf, rbuf, bsize) == 0;
        if (!ok) {
            xil_printf("блок %u: ошибка записи/проверки, помечаю плохим\r\n", (unsigned)block);
            XNandPs_MarkBlockBad(&nand, block);
            block++; bad++;
            continue;
        }
        done += n;
        block++;
        MB_DONE = done;
        Xil_DCacheFlushRange(MB_BASE, 32);
        xil_printf("\r%u / %u КБ", (unsigned)(done >> 10), (unsigned)(len >> 10));
    }

    xil_printf("\r\nГотово: %u байт в %u блоках, плохих пропущено %u\r\n",
               (unsigned)len, (unsigned)block, (unsigned)bad);
    set_status(ST_OK);
    for (;;)
        ;
}
