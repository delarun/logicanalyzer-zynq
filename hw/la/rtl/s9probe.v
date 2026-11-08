// =====================================================================
// s9probe — интерфейсы отладки на свободных линиях S9: JTAG, SWD, SPI, UART, I2C/GPIO
//
// Сдвиговый движок: команда до 8 бит, на каждый бит
//   выставить выходы → такт низкий (DIV+1) → в конце низкой фазы прочитать вход
//   → такт высокий (DIV+1). Такт в покое — высокий (JTAG/SWD; SPI — режим 3).
//   JTAG: TCK=p0, TMS=A, TDI=B, вход TDO=p3
//   SWD:  SWCLK=p0, SWDIO=p1 (выход A при AOE=1, иначе отпущена), вход SWDIO=p1
//   SPI:  SCK=p7, MOSI=A, вход MISO=p9 (порядок бит — младший первым; MSB-first
//         делает прошивка разворотом байта)
// UART 8N1 с FIFO по 64 байта. Остальные линии — GPIO (выход/третье состояние).
//
// Линии (индекс → разъём):
//   0 J9 TX   TCK/SWCLK      7  FAN4 SPEED  SPI SCK
//   1 J9 RST  TMS/SWDIO      8  FAN5 SPEED  SPI MOSI
//   2 J9 PLUG TDI            9  FAN6 SPEED  SPI MISO
//   3 J9 RX   TDO           10  SCL1        SPI CS (GPIO)
//   4 FAN1    nRESET (GPIO) 11  SCL2        I2C SCL (GPIO, открытый сток)
//   5 FAN2    UART TX       12  SDA2        I2C SDA (GPIO, открытый сток)
//   6 FAN3    UART RX       13  SDA1        резерв (GPIO)
//
// Регистры:
//   0x00 ID      R  0x50524231 ("PRB1")
//   0x04 CTRL    RW bit0 JTAG (p0..p2 — движок), bit1 SWD (p0, p1 — движок),
//                   bit2 SPI (p7, p8 — движок), bit3 UART (p5 — передатчик)
//   0x08 DIV     RW полупериод такта = DIV+1 тактов aclk (DIV >= 2, т.е. до 16.7 МГц)
//   0x0C GPO     RW выходные уровни GPIO-линий
//   0x10 GOE     RW разрешение выхода GPIO-линий (0 — третье состояние)
//   0x14 GPI     R  уровни всех линий
//   0x18 CMD     W  [7:0] A, [15:8] B, [23:16] AOE, [26:24] бит-1, [28:27] 0 JTAG/1 SWD/2 SPI,
//                   [29] без ответа
//   0x1C RES     R  bit31 — есть данные, [7:0] прочитанные биты (чтение снимает из FIFO)
//   0x20 STAT    R  [6:0] занято в CMD FIFO, [14:8] в RES FIFO, bit16 движок занят
//   0x24 UDIV    RW делитель UART: aclk / бод
//   0x28 UTX     W  байт в передатчик;  R: [6:0] свободно в TX FIFO
//   0x2C URX     R  bit31 — есть байт, bit30 — было переполнение (сбрасывается чтением), [7:0]
// =====================================================================
`timescale 1ns / 1ps
`default_nettype none

module s9probe #(
    parameter integer NP = 14
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET aresetn" *)
    input  wire          aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire          aresetn,

    // Линии: выход, разрешение выхода, вход (буферы IOBUF — снаружи)
    output reg  [NP-1:0] pin_o,
    output reg  [NP-1:0] pin_oe,
    input  wire [NP-1:0] pin_i,

    // AXI-Lite
    input  wire [6:0]  s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output reg         s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output reg         s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [6:0]  s_axi_araddr,
    input  wire        s_axi_arvalid,
    output reg         s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);

    localparam P_TCK = 0, P_TMS = 1, P_TDI = 2, P_TDO = 3, P_UTX = 5, P_URX = 6,
               P_SCK = 7, P_MOSI = 8, P_MISO = 9;

    // -----------------------------------------------------------------
    // Синхронизация входов
    // -----------------------------------------------------------------
    (* IOB = "TRUE" *) reg [NP-1:0] in_m;
    reg [NP-1:0] in_s;
    always @(posedge aclk) begin
        in_m <= pin_i;
        in_s <= in_m;
    end

    // -----------------------------------------------------------------
    // Регистры
    // -----------------------------------------------------------------
    reg [3:0]    r_ctrl;
    reg [15:0]   r_div;
    reg [NP-1:0] r_gpo, r_goe;
    reg [15:0]   r_udiv;

    // FIFO команд и ответов (распределённая память, 64 слова)
    reg [29:0] cmd_mem [0:63];
    reg [6:0]  cmd_wr, cmd_rd;
    wire [6:0] cmd_cnt  = cmd_wr - cmd_rd;
    wire       cmd_full = cmd_cnt[6];
    wire       cmd_empty = (cmd_cnt == 7'd0);

    reg [7:0]  res_mem [0:63];
    reg [6:0]  res_wr, res_rd;
    wire [6:0] res_cnt  = res_wr - res_rd;
    wire       res_empty = (res_cnt == 7'd0);

    // UART FIFO
    reg [7:0]  utx_mem [0:63];
    reg [6:0]  utx_wr, utx_rd;
    wire [6:0] utx_cnt  = utx_wr - utx_rd;
    reg [7:0]  urx_mem [0:63];
    reg [6:0]  urx_wr, urx_rd;
    wire [6:0] urx_cnt  = urx_wr - urx_rd;
    reg        urx_ovf;

    reg        eng_busy;

    // запись по AXI-Lite
    reg        cmd_push, utx_push;
    reg [29:0] cmd_push_d;
    reg [7:0]  utx_push_d;
    reg        res_pop, urx_pop, urx_ovf_clr;

    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;

    always @(posedge aclk) begin
        cmd_push <= 1'b0;
        utx_push <= 1'b0;
        if (!aresetn) begin
            s_axi_awready <= 1'b0; s_axi_wready <= 1'b0; s_axi_bvalid <= 1'b0;
            r_ctrl <= 4'd0; r_div <= 16'd4; r_gpo <= {NP{1'b0}}; r_goe <= {NP{1'b0}};
            r_udiv <= 16'd868;     // 115200 при 100 МГц
        end else begin
            if (!s_axi_bvalid && !s_axi_awready && s_axi_awvalid && s_axi_wvalid) begin
                s_axi_awready <= 1'b1;
                s_axi_wready  <= 1'b1;
                s_axi_bvalid  <= 1'b1;
                case (s_axi_awaddr[6:2])
                    5'h01: r_ctrl <= s_axi_wdata[3:0];
                    5'h02: r_div  <= (s_axi_wdata[15:0] < 16'd2) ? 16'd2 : s_axi_wdata[15:0];
                    5'h03: r_gpo  <= s_axi_wdata[NP-1:0];
                    5'h04: r_goe  <= s_axi_wdata[NP-1:0];
                    5'h06: begin cmd_push <= 1'b1; cmd_push_d <= s_axi_wdata[29:0]; end
                    5'h09: r_udiv <= (s_axi_wdata[15:0] < 16'd8) ? 16'd8 : s_axi_wdata[15:0];
                    5'h0A: begin utx_push <= 1'b1; utx_push_d <= s_axi_wdata[7:0]; end
                    default: ;
                endcase
            end else begin
                s_axi_awready <= 1'b0;
                s_axi_wready  <= 1'b0;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
        end
    end

    // чтение по AXI-Lite (RES и URX снимают данные из FIFO)
    always @(posedge aclk) begin
        res_pop <= 1'b0;
        urx_pop <= 1'b0;
        urx_ovf_clr <= 1'b0;
        if (!aresetn) begin
            s_axi_arready <= 1'b0; s_axi_rvalid <= 1'b0; s_axi_rdata <= 32'd0;
        end else begin
            if (!s_axi_rvalid && !s_axi_arready && s_axi_arvalid) begin
                s_axi_arready <= 1'b1;
                s_axi_rvalid  <= 1'b1;
                case (s_axi_araddr[6:2])
                    5'h00: s_axi_rdata <= 32'h5052_4231;
                    5'h01: s_axi_rdata <= {28'd0, r_ctrl};
                    5'h02: s_axi_rdata <= {16'd0, r_div};
                    5'h03: s_axi_rdata <= r_gpo;
                    5'h04: s_axi_rdata <= r_goe;
                    5'h05: s_axi_rdata <= in_s;
                    5'h07: begin
                        s_axi_rdata <= {!res_empty, 23'd0, res_mem[res_rd[5:0]]};
                        res_pop <= !res_empty;
                    end
                    5'h08: s_axi_rdata <= {15'd0, eng_busy, 1'b0, res_cnt, 1'b0, cmd_cnt};
                    5'h09: s_axi_rdata <= {16'd0, r_udiv};
                    5'h0A: s_axi_rdata <= {25'd0, 7'd64 - utx_cnt};
                    5'h0B: begin
                        s_axi_rdata <= {(urx_cnt != 7'd0), urx_ovf, 22'd0, urx_mem[urx_rd[5:0]]};
                        urx_pop <= (urx_cnt != 7'd0);
                        urx_ovf_clr <= 1'b1;
                    end
                    default: s_axi_rdata <= 32'd0;
                endcase
            end else begin
                s_axi_arready <= 1'b0;
            end
            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
        end
    end

    // -----------------------------------------------------------------
    // Сдвиговый движок
    // -----------------------------------------------------------------
    reg  [29:0] cur;
    reg  [2:0]  bitn;
    reg  [7:0]  inbits;
    reg  [15:0] cnt;
    reg         phase_hi;
    reg         e_clk, e_a, e_b, e_aoe;   // выходы движка
    reg         res_push;
    reg  [7:0]  res_push_d;

    wire [1:0]  c_mode = cur[28:27];
    wire [2:0]  c_last = cur[26:24];
    // Выборка с первого триггера (in_m): с учётом регистра на выходе и задержки
    // синхронизатора это ~1 такт до подъёма такта на ножке. in_s — для GPI/UART.
    wire        sample = (c_mode == 2'd0) ? in_m[P_TDO] :
                         (c_mode == 2'd1) ? in_m[P_TMS] : in_m[P_MISO];

    always @(posedge aclk) begin
        res_push <= 1'b0;
        if (!aresetn) begin
            eng_busy <= 1'b0;
            e_clk <= 1'b1; e_a <= 1'b1; e_b <= 1'b1; e_aoe <= 1'b0;
            cnt <= 16'd0; phase_hi <= 1'b0; bitn <= 3'd0; inbits <= 8'd0;
        end else if (!eng_busy) begin
            if (!cmd_empty) begin
                cur      <= cmd_mem[cmd_rd[5:0]];
                eng_busy <= 1'b1;
                bitn     <= 3'd0;
                inbits   <= 8'd0;
                cnt      <= 16'd0;
                phase_hi <= 1'b0;
                // первый бит: выходы и такт вниз
                e_a   <= cmd_mem[cmd_rd[5:0]][0];
                e_b   <= cmd_mem[cmd_rd[5:0]][8];
                e_aoe <= cmd_mem[cmd_rd[5:0]][16];
                e_clk <= 1'b0;
            end
        end else begin
            if (cnt != r_div) begin
                cnt <= cnt + 16'd1;
            end else begin
                cnt <= 16'd0;
                if (!phase_hi) begin
                    // конец низкой фазы: читаем вход, такт вверх
                    inbits[bitn] <= sample;
                    e_clk    <= 1'b1;
                    phase_hi <= 1'b1;
                end else if (bitn == c_last) begin
                    // команда выполнена
                    eng_busy   <= 1'b0;
                    res_push   <= !cur[29];
                    res_push_d <= inbits;
                end else begin
                    // следующий бит
                    bitn     <= bitn + 3'd1;
                    e_a      <= cur[bitn + 3'd1];
                    e_b      <= cur[8 + bitn + 3'd1];
                    e_aoe    <= cur[16 + bitn + 3'd1];
                    e_clk    <= 1'b0;
                    phase_hi <= 1'b0;
                end
            end
        end
    end

    wire cmd_pop = !eng_busy && !cmd_empty && aresetn;

    // указатели FIFO команд/ответов
    always @(posedge aclk) begin
        if (!aresetn) begin
            cmd_wr <= 7'd0; cmd_rd <= 7'd0; res_wr <= 7'd0; res_rd <= 7'd0;
        end else begin
            if (cmd_push && !cmd_full) begin
                cmd_mem[cmd_wr[5:0]] <= cmd_push_d;
                cmd_wr <= cmd_wr + 7'd1;
            end
            if (cmd_pop) cmd_rd <= cmd_rd + 7'd1;
            if (res_push && !res_cnt[6]) begin
                res_mem[res_wr[5:0]] <= res_push_d;
                res_wr <= res_wr + 7'd1;
            end
            if (res_pop) res_rd <= res_rd + 7'd1;
        end
    end

    // -----------------------------------------------------------------
    // UART 8N1
    // -----------------------------------------------------------------
    reg [15:0] tx_cnt;  reg [3:0] tx_bit;  reg [9:0] tx_sh;  reg tx_busy;  reg tx_line;
    reg [15:0] rx_cnt;  reg [3:0] rx_bit;  reg [7:0] rx_sh;  reg rx_busy;
    reg        rx_push;  reg [7:0] rx_push_d;
    wire       rxd = in_s[P_URX];

    always @(posedge aclk) begin
        rx_push <= 1'b0;
        if (!aresetn) begin
            tx_busy <= 1'b0; tx_line <= 1'b1; tx_cnt <= 16'd0; tx_bit <= 4'd0;
            rx_busy <= 1'b0; rx_cnt <= 16'd0; rx_bit <= 4'd0;
            utx_rd <= 7'd0; utx_wr <= 7'd0; urx_wr <= 7'd0; urx_rd <= 7'd0; urx_ovf <= 1'b0;
        end else begin
            // FIFO передатчика
            if (utx_push && !utx_cnt[6]) begin
                utx_mem[utx_wr[5:0]] <= utx_push_d;
                utx_wr <= utx_wr + 7'd1;
            end
            // передатчик
            if (!tx_busy) begin
                tx_line <= 1'b1;
                if (utx_cnt != 7'd0) begin
                    tx_sh   <= {1'b1, utx_mem[utx_rd[5:0]], 1'b0};
                    utx_rd  <= utx_rd + 7'd1;
                    tx_busy <= 1'b1;
                    tx_bit  <= 4'd0;
                    tx_cnt  <= 16'd0;
                end
            end else begin
                tx_line <= tx_sh[0];
                if (tx_cnt == r_udiv - 16'd1) begin
                    tx_cnt <= 16'd0;
                    tx_sh  <= {1'b1, tx_sh[9:1]};
                    if (tx_bit == 4'd9) tx_busy <= 1'b0;
                    tx_bit <= tx_bit + 4'd1;
                end else begin
                    tx_cnt <= tx_cnt + 16'd1;
                end
            end
            // приёмник: середина бита
            if (!rx_busy) begin
                if (!rxd) begin
                    rx_busy <= 1'b1;
                    rx_cnt  <= {1'b0, r_udiv[15:1]};
                    rx_bit  <= 4'd0;
                end
            end else if (rx_cnt == 16'd0) begin
                rx_cnt <= r_udiv - 16'd1;
                if (rx_bit == 4'd0) begin
                    if (rxd) rx_busy <= 1'b0;      // ложный старт
                end else if (rx_bit <= 4'd8) begin
                    rx_sh <= {rxd, rx_sh[7:1]};
                end else begin
                    rx_busy <= 1'b0;
                    if (rxd) begin                  // стоп-бит в порядке
                        rx_push   <= 1'b1;
                        rx_push_d <= rx_sh;
                    end
                end
                rx_bit <= rx_bit + 4'd1;
            end else begin
                rx_cnt <= rx_cnt - 16'd1;
            end
            // FIFO приёмника
            if (rx_push) begin
                if (urx_cnt[6]) urx_ovf <= 1'b1;
                else begin
                    urx_mem[urx_wr[5:0]] <= rx_push_d;
                    urx_wr <= urx_wr + 7'd1;
                end
            end else if (urx_ovf_clr) begin
                urx_ovf <= 1'b0;
            end
            if (urx_pop) urx_rd <= urx_rd + 7'd1;
        end
    end

    // -----------------------------------------------------------------
    // Мультиплексор линий
    // -----------------------------------------------------------------
    wire jtag_en = r_ctrl[0], swd_en = r_ctrl[1], spi_en = r_ctrl[2], uart_en = r_ctrl[3];
    always @(posedge aclk) begin
        pin_o  <= r_gpo;
        pin_oe <= r_goe;
        if (jtag_en || swd_en) begin
            pin_o[P_TCK] <= e_clk;  pin_oe[P_TCK] <= 1'b1;
            pin_o[P_TMS] <= e_a;    pin_oe[P_TMS] <= jtag_en ? 1'b1 : e_aoe;
        end
        if (jtag_en) begin
            pin_o[P_TDI] <= e_b;    pin_oe[P_TDI] <= 1'b1;
            pin_oe[P_TDO] <= 1'b0;
        end
        if (spi_en) begin
            pin_o[P_SCK]  <= e_clk; pin_oe[P_SCK]  <= 1'b1;
            pin_o[P_MOSI] <= e_a;   pin_oe[P_MOSI] <= 1'b1;
            pin_oe[P_MISO] <= 1'b0;
        end
        if (uart_en) begin
            pin_o[P_UTX] <= tx_line; pin_oe[P_UTX] <= 1'b1;
            pin_oe[P_URX] <= 1'b0;
        end
    end

endmodule

`default_nettype wire
