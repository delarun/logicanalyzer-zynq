// Регистры ядра s9la_core (hw/la/rtl/s9la_core.v) и раскладка памяти
#pragma once
#include "xil_io.h"
#include "xil_types.h"

#define S9LA_BASE       0x43C00000u

#define LA_CTRL         0x00
#define LA_STATUS       0x04
#define LA_DIV          0x08
#define LA_UNIT         0x0C
#define LA_RBASE        0x10
#define LA_RSIZE        0x14
#define LA_PRE          0x18
#define LA_POST         0x1C
#define LA_TMASK        0x20
#define LA_TVAL         0x24
#define LA_TRISE        0x28
#define LA_TFALL        0x2C
#define LA_TRIGPOS      0x30
#define LA_COUNT        0x34
#define LA_ID           0x38
#define LA_INPUT        0x3C
#define LA_LED          0x40
#define LA_WORDS        0x44
#define LA_DNA_LO       0x48
#define LA_DNA_HI       0x4C
#define LA_TDIV         0x50   // меандр на J18 (пин 4 FAN): f = 100 МГц / (2 * (TDIV + 1)), 0 — выкл

#define LA_CTRL_START   (1u << 0)
#define LA_CTRL_STOP    (1u << 1)
#define LA_CTRL_TEST    (1u << 4)

#define LA_ST_MASK      0x7u
#define LA_ST_IDLE      0u
#define LA_ST_CAPTURE   1u
#define LA_ST_FLUSH     2u
#define LA_ST_DRAIN     3u
#define LA_ST_DONE      4u
#define LA_ST_TRIGGERED (1u << 4)
#define LA_ST_OVERFLOW  (1u << 5)
#define LA_ST_AXIERR    (1u << 6)

#define LA_ID_VALUE     0x53394C41u   // "S9LA"
#define LA_CLOCK_HZ     100000000u
#define LA_CHANNELS     32   // J1..J8: RX, TX, RST, PLUG

// Кольцевой буфер выборок в DDR: 0x0800_0000 .. 0x1FFF_FFFF (384 МБ).
// Программа и куча lwIP живут ниже 0x0800_0000 (см. lscript.ld).
#define RING_BASE       0x08000000u
#define RING_SIZE       0x18000000u

// Светодиоды D5..D7 (D8 зажигается аппаратно во время захвата)
#define LED_NET         (1u << 0)
#define LED_CLIENT      (1u << 1)
#define LED_SEND        (1u << 2)

static inline u32  la_rd(u32 off)        { return Xil_In32(S9LA_BASE + off); }
static inline void la_wr(u32 off, u32 v) { Xil_Out32(S9LA_BASE + off, v); }
