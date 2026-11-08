# Общая конфигурация PS7 для Antminer S9 v1.3 (DDR, MIO, тактирование).
# Использование: source hw/common/ps7_s9.tcl; s9_ps7_config $ps
# Тайминги NAND — как в стоковом U-Boot S9 (SMC set_cycles = 0x00449144, такт SMC 10 нс).
# Значения Vivado по умолчанию (t_RC = 11 нс) для этой микросхемы слишком быстрые:
# READ ID возвращает мусор, ONFI не распознаётся, FSBL не загрузится с NAND.
proc s9_ps7_config {ps} {
    set_property -dict [list \
        CONFIG.PCW_CRYSTAL_PERIPHERAL_FREQMHZ   {33.333333} \
        CONFIG.PCW_APU_PERIPHERAL_FREQMHZ       {666.666666} \
        CONFIG.PCW_PRESET_BANK0_VOLTAGE         {LVCMOS 3.3V} \
        CONFIG.PCW_PRESET_BANK1_VOLTAGE         {LVCMOS 1.8V} \
        \
        CONFIG.PCW_UIPARAM_DDR_MEMORY_TYPE      {DDR 3} \
        CONFIG.PCW_UIPARAM_DDR_PARTNO           {MT41K128M16 JT-125} \
        CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH        {32 Bit} \
        CONFIG.PCW_UIPARAM_DDR_FREQ_MHZ         {533.333333} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_WRITE_LEVEL {1} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_READ_GATE  {1} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_DATA_EYE   {1} \
        \
        CONFIG.PCW_NAND_PERIPHERAL_ENABLE       {1} \
        CONFIG.PCW_NAND_GRP_D8_ENABLE           {0} \
        CONFIG.PCW_NAND_CYCLES_T_RC             {40} \
        CONFIG.PCW_NAND_CYCLES_T_WC             {40} \
        CONFIG.PCW_NAND_CYCLES_T_REA            {10} \
        CONFIG.PCW_NAND_CYCLES_T_WP             {20} \
        CONFIG.PCW_NAND_CYCLES_T_CLR            {20} \
        CONFIG.PCW_NAND_CYCLES_T_AR             {20} \
        CONFIG.PCW_NAND_CYCLES_T_RR             {40} \
        \
        CONFIG.PCW_ENET0_PERIPHERAL_ENABLE      {1} \
        CONFIG.PCW_ENET0_ENET0_IO               {MIO 16 .. 27} \
        CONFIG.PCW_ENET0_GRP_MDIO_ENABLE        {1} \
        CONFIG.PCW_ENET0_GRP_MDIO_IO            {MIO 52 .. 53} \
        CONFIG.PCW_ENET0_PERIPHERAL_FREQMHZ     {1000 Mbps} \
        \
        CONFIG.PCW_SD0_PERIPHERAL_ENABLE        {1} \
        CONFIG.PCW_SD0_SD0_IO                   {MIO 40 .. 45} \
        CONFIG.PCW_SD0_GRP_CD_ENABLE            {1} \
        CONFIG.PCW_SD0_GRP_CD_IO                {MIO 46} \
        CONFIG.PCW_SD0_GRP_WP_ENABLE            {1} \
        CONFIG.PCW_SD0_GRP_WP_IO                {MIO 50} \
        \
        CONFIG.PCW_UART1_PERIPHERAL_ENABLE      {1} \
        CONFIG.PCW_UART1_UART1_IO               {MIO 48 .. 49} \
        CONFIG.PCW_UART1_BAUD_RATE              {115200} \
        \
        CONFIG.PCW_GPIO_MIO_GPIO_ENABLE         {1} \
        CONFIG.PCW_TTC0_PERIPHERAL_ENABLE       {1} \
        CONFIG.PCW_QSPI_PERIPHERAL_ENABLE       {0} \
        CONFIG.PCW_USB0_PERIPHERAL_ENABLE       {0} \
        \
        CONFIG.PCW_USE_M_AXI_GP0                {1} \
        CONFIG.PCW_EN_CLK0_PORT                 {1} \
        CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ     {100} \
    ] $ps
}
