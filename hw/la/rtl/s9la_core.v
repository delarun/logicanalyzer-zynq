// =====================================================================
// s9la_core — ядро логического анализатора Antminer S9
//
//   входы → 2 триггера синхронизации → делитель частоты → аппаратный триггер
//   → упаковка выборок (1/2/4 байта) в слова 64 бита → FIFO (BRAM)
//   → AXI4-мастер записи пачками по 16 слов в кольцевой буфер в DDR (через HP0)
//
// Один тактовый домен: aclk (FCLK0, 100 МГц) — и выборка, и AXI.
//
// Регистры (AXI-Lite, смещения в байтах):
//   0x00 CTRL    W: bit0 START (импульс), bit1 STOP (импульс)
//                RW: bit4 TEST (вместо входов — номер выборки)
//   0x04 STATUS  R: bit[2:0] состояние (0 IDLE, 1 CAPTURE, 2 FLUSH, 3 DRAIN, 4 DONE),
//                   bit4 TRIGGERED, bit5 OVERFLOW (FIFO переполнилось), bit6 AXI_ERROR
//   0x08 DIV     выборка каждые DIV+1 тактов aclk
//   0x0C UNIT    размер выборки: 0 — 1 байт, 1 — 2 байта, 2 — 4 байта
//   0x10 BASE    адрес кольцевого буфера (кратно 128, лучше 4096)
//   0x14 SIZE    размер буфера в байтах (кратно 128)
//   0x18 PRE     выборок до триггера (триггер не срабатывает раньше)
//   0x1C POST    выборок начиная с триггерной (>= 1)
//   0x20 TMASK   маска каналов для условия по уровню
//   0x24 TVAL    требуемые уровни
//   0x28 TRISE   каналы, на которых нужен фронт 0→1
//   0x2C TFALL   каналы, на которых нужен спад 1→0
//   0x30 TRIGPOS R: номер триггерной выборки
//   0x34 COUNT   R: всего выборок записано
//   0x38 ID      R: 0x53394C41 ("S9LA")
//   0x3C INPUT   R: текущее состояние входов
//   0x40 LED     RW: светодиоды D5..D7 (D8 горит во время захвата)
//   0x44 WORDS   R: слов 64 бита записано в DDR (для отладки)
//   0x48 DNA_LO  R: Device DNA [31:0]
//   0x4C DNA_HI  R: bit31 — DNA прочитан, [24:0] — DNA [56:32]
//   0x50 TDIV    RW: тестовый меандр на J9 TX, J9 RST и J18 (PWM вентиляторов):
//                    f = aclk / (2 * (TDIV + 1)), 0 — выключен (низкий уровень)
//
// Каналы (бит выборки): по 4 на разъём хешплаты J1..J8 — RX, TX, RST, PLUG.
// Все эти линии идут с банка 34 прямо на разъём (схема платы, лист 13).
//
// Все условия триггера объединяются по И. Пустые маски — срабатывание сразу
// после PRE выборок. Кольцо должно вмещать PRE + POST выборок.
// =====================================================================
`timescale 1ns / 1ps
`default_nettype none

module s9la_core #(
    parameter integer FIFO_AW = 9,         // 2^9 = 512 слов по 64 бита (4 КБ)
    parameter integer USE_DNA = 1          // 0 — для симуляции без unisim
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:m_axi, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,

    // Входы анализатора: разъёмы хешплат J1..J8
    input  wire [7:0]  hb_rx,
    input  wire [7:0]  hb_tx,
    input  wire [7:0]  hb_rst,
    input  wire [7:0]  hb_plug,
    // Тестовые выходы: меандр TDIV или 0
    output wire        test_tx,      // J9 TX
    output wire        test_rst,     // J9 RST
    output wire        fan_pwm,      // J18, пин 4 разъёмов FAN
    output wire [3:0]  led,

    // AXI-Lite slave
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
    input  wire        s_axi_rready,

    // AXI4 master, только запись (64 бита)
    output wire [31:0] m_axi_awaddr,
    output wire [7:0]  m_axi_awlen,
    output wire [2:0]  m_axi_awsize,
    output wire [1:0]  m_axi_awburst,
    output wire [3:0]  m_axi_awcache,
    output wire [2:0]  m_axi_awprot,
    output reg         m_axi_awvalid,
    input  wire        m_axi_awready,
    output wire [63:0] m_axi_wdata,
    output wire [7:0]  m_axi_wstrb,
    output wire        m_axi_wlast,
    output wire        m_axi_wvalid,
    input  wire        m_axi_wready,
    input  wire [1:0]  m_axi_bresp,
    input  wire        m_axi_bvalid,
    output wire        m_axi_bready
);

    localparam [2:0] ST_IDLE = 3'd0, ST_CAPT = 3'd1, ST_FLUSH = 3'd2, ST_DRAIN = 3'd3, ST_DONE = 3'd4;
    localparam integer DEPTH = 1 << FIFO_AW;

    // -----------------------------------------------------------------
    // Регистры управления
    // -----------------------------------------------------------------
    reg        r_test;
    reg [31:0] r_div, r_base, r_size, r_pre, r_post, r_tmask, r_tval, r_trise, r_tfall;
    reg [1:0]  r_unit;
    reg [2:0]  r_led;
    reg [31:0] r_tdiv;
    reg        start_pulse, stop_pulse;

    reg [2:0]  state;
    reg        triggered, overflow, axi_err;
    reg [31:0] trigpos, scount, words_done;
    reg [56:0] dna;
    reg        dna_done;

    // -----------------------------------------------------------------
    // Входы: IOB-регистр + второй триггер синхронизации
    // -----------------------------------------------------------------
    // бит 4*n + {0 RX, 1 TX, 2 RST, 3 PLUG} — разъём J(n+1)
    wire [31:0] pins;
    genvar gp;
    generate
        for (gp = 0; gp < 8; gp = gp + 1) begin : g_pins
            assign pins[4*gp +: 4] = {hb_plug[gp], hb_rst[gp], hb_tx[gp], hb_rx[gp]};
        end
    endgenerate
    (* IOB = "TRUE" *) reg [31:0] in_iob;
    reg [31:0] in_sync;
    always @(posedge aclk) begin
        in_iob  <= pins;
        in_sync <= in_iob;
    end

    // -----------------------------------------------------------------
    // AXI-Lite
    // -----------------------------------------------------------------
    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;

    always @(posedge aclk) begin
        start_pulse <= 1'b0;
        stop_pulse  <= 1'b0;
        if (!aresetn) begin
            s_axi_awready <= 1'b0;
            s_axi_wready  <= 1'b0;
            s_axi_bvalid  <= 1'b0;
            r_test <= 1'b0;
            r_div  <= 32'd0; r_unit <= 2'd0;
            r_base <= 32'h1000_0000; r_size <= 32'h0100_0000;
            r_pre  <= 32'd0; r_post <= 32'd1;
            r_tmask <= 32'd0; r_tval <= 32'd0; r_trise <= 32'd0; r_tfall <= 32'd0;
            r_led  <= 3'd0;
            r_tdiv <= 32'd0;
        end else begin
            if (!s_axi_bvalid && !s_axi_awready && s_axi_awvalid && s_axi_wvalid) begin
                s_axi_awready <= 1'b1;
                s_axi_wready  <= 1'b1;
                s_axi_bvalid  <= 1'b1;
                case (s_axi_awaddr[6:2])
                    5'h00: begin
                        start_pulse <= s_axi_wdata[0];
                        stop_pulse  <= s_axi_wdata[1];
                        r_test      <= s_axi_wdata[4];
                    end
                    5'h02: r_div   <= s_axi_wdata;
                    5'h03: r_unit  <= s_axi_wdata[1:0];
                    5'h04: r_base  <= {s_axi_wdata[31:7], 7'd0};
                    5'h05: r_size  <= {s_axi_wdata[31:7], 7'd0};
                    5'h06: r_pre   <= s_axi_wdata;
                    5'h07: r_post  <= s_axi_wdata;
                    5'h08: r_tmask <= s_axi_wdata;
                    5'h09: r_tval  <= s_axi_wdata;
                    5'h0A: r_trise <= s_axi_wdata;
                    5'h0B: r_tfall <= s_axi_wdata;
                    5'h10: r_led   <= s_axi_wdata[2:0];
                    5'h14: r_tdiv  <= s_axi_wdata;
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

    always @(posedge aclk) begin
        if (!aresetn) begin
            s_axi_arready <= 1'b0;
            s_axi_rvalid  <= 1'b0;
            s_axi_rdata   <= 32'd0;
        end else begin
            if (!s_axi_rvalid && !s_axi_arready && s_axi_arvalid) begin
                s_axi_arready <= 1'b1;
                s_axi_rvalid  <= 1'b1;
                case (s_axi_araddr[6:2])
                    5'h00: s_axi_rdata <= {27'd0, r_test, 4'd0};
                    5'h01: s_axi_rdata <= {25'd0, axi_err, overflow, triggered, 1'b0, state};
                    5'h02: s_axi_rdata <= r_div;
                    5'h03: s_axi_rdata <= {30'd0, r_unit};
                    5'h04: s_axi_rdata <= r_base;
                    5'h05: s_axi_rdata <= r_size;
                    5'h06: s_axi_rdata <= r_pre;
                    5'h07: s_axi_rdata <= r_post;
                    5'h08: s_axi_rdata <= r_tmask;
                    5'h09: s_axi_rdata <= r_tval;
                    5'h0A: s_axi_rdata <= r_trise;
                    5'h0B: s_axi_rdata <= r_tfall;
                    5'h0C: s_axi_rdata <= trigpos;
                    5'h0D: s_axi_rdata <= scount;
                    5'h0E: s_axi_rdata <= 32'h5339_4C41;
                    5'h0F: s_axi_rdata <= in_sync;
                    5'h10: s_axi_rdata <= {29'd0, r_led};
                    5'h11: s_axi_rdata <= words_done;
                    5'h12: s_axi_rdata <= dna[31:0];
                    5'h13: s_axi_rdata <= {dna_done, 6'd0, dna[56:32]};
                    5'h14: s_axi_rdata <= r_tdiv;
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
    // Device DNA (57 бит) — уникальный номер кристалла, для MAC-адреса.
    // DNA_PORT тактируется aclk/8 (12.5 МГц, предел — 100 МГц).
    // -----------------------------------------------------------------
    reg  [2:0]  dna_ph;
    reg  [5:0]  dna_stage;
    reg         dna_read, dna_shift;
    wire        dna_dout;
    wire        dna_clk = dna_ph[2];

    always @(posedge aclk) begin
        if (!aresetn) begin
            dna_ph <= 3'd0; dna_stage <= 6'd0;
            dna_read <= 1'b0; dna_shift <= 1'b0; dna_done <= 1'b0;
            dna <= 57'd0;
        end else begin
            dna_ph <= dna_ph + 3'd1;
            if (dna_ph == 3'd3 && !dna_done) begin      // DNA_PORT.CLK низкий, фронт через такт
                if (dna_stage == 6'd0) begin
                    dna_read <= 1'b1;
                end else if (dna_stage <= 6'd57) begin
                    dna <= {dna[55:0], dna_dout};
                    dna_read  <= 1'b0;
                    dna_shift <= 1'b1;
                end else begin
                    dna_shift <= 1'b0;
                    dna_done  <= 1'b1;
                end
                dna_stage <= dna_stage + 6'd1;
            end
        end
    end

    generate
        if (USE_DNA) begin : g_dna
            DNA_PORT #(.SIM_DNA_VALUE(57'h0)) u_dna (
                .DOUT(dna_dout), .CLK(dna_clk), .DIN(1'b0), .READ(dna_read), .SHIFT(dna_shift)
            );
        end else begin : g_nodna
            assign dna_dout = 1'b1;
        end
    endgenerate

    // -----------------------------------------------------------------
    // Светодиоды и тестовый меандр (J9 TX, J9 RST, J18)
    // -----------------------------------------------------------------
    assign led = {state == ST_CAPT, r_led};

    reg [31:0] tdiv_cnt;
    reg        test_sq;
    always @(posedge aclk) begin
        if (!aresetn || r_tdiv == 32'd0) begin
            tdiv_cnt <= 32'd0;
            test_sq  <= 1'b0;
        end else if (tdiv_cnt >= r_tdiv) begin
            tdiv_cnt <= 32'd0;
            test_sq  <= ~test_sq;
        end else begin
            tdiv_cnt <= tdiv_cnt + 32'd1;
        end
    end
    assign test_tx  = test_sq;
    assign test_rst = test_sq;
    assign fan_pwm  = test_sq;

    // -----------------------------------------------------------------
    // FIFO 64 бита (BRAM), чтение «с упреждением»: fifo_q всегда = mem[rd_ptr]
    // -----------------------------------------------------------------
    reg [63:0] mem [0:DEPTH-1];
    reg [FIFO_AW:0] wr_ptr, wr_ptr_d, rd_ptr;
    reg [63:0] fifo_q;
    wire       fifo_push;
    wire [63:0] fifo_din;
    wire       fifo_pop;
    wire [FIFO_AW:0] cnt_wr = wr_ptr - rd_ptr;     // для проверки переполнения
    wire [FIFO_AW:0] cnt_rd = wr_ptr_d - rd_ptr;   // видимое чтению (данные уже в BRAM)
    wire       fifo_full  = (cnt_wr >= DEPTH - 1);
    wire       fifo_avail = (cnt_rd != 0);
    wire [FIFO_AW:0] rd_ptr_n = rd_ptr + (fifo_pop ? 1'b1 : 1'b0);

    always @(posedge aclk) begin
        if (fifo_push && !fifo_full)
            mem[wr_ptr[FIFO_AW-1:0]] <= fifo_din;
        fifo_q <= mem[rd_ptr_n[FIFO_AW-1:0]];
    end

    // -----------------------------------------------------------------
    // Захват: делитель, триггер, упаковка
    // -----------------------------------------------------------------
    reg [31:0] div_cnt;
    reg [31:0] prev;
    reg        have_prev;
    reg [31:0] stop_at;
    reg [63:0] pack;
    reg [3:0]  pos;          // заполнено байт в pack
    reg        push_r;
    reg [63:0] push_data;
    reg [2:0]  settle;

    wire        tick = (state == ST_CAPT) && (div_cnt == 32'd0);
    wire [31:0] smp  = r_test ? scount : in_sync;
    wire [31:0] edge_prev = have_prev ? prev : smp;
    wire level_ok = (((smp ^ r_tval) & r_tmask) == 32'd0);
    wire rise_ok  = ((r_trise & ~(smp & ~edge_prev)) == 32'd0);
    wire fall_ok  = ((r_tfall & ~(~smp & edge_prev)) == 32'd0);
    wire trig_now = !triggered && (scount >= r_pre) && level_ok && rise_ok && fall_ok;
    wire [31:0] stop_at_n = trig_now ? (scount + r_post) : stop_at;
    wire last_smp = (triggered || trig_now) && (scount + 32'd1 == stop_at_n);

    // Слово после вставки текущей выборки
    reg [63:0] pack_n;
    reg [3:0]  pos_n;
    always @(*) begin
        pack_n = pack;
        case (r_unit)
            2'd0:    begin pack_n[pos*8 +: 8]  = smp[7:0];  pos_n = pos + 4'd1; end
            2'd1:    begin pack_n[pos*8 +: 16] = smp[15:0]; pos_n = pos + 4'd2; end
            default: begin pack_n[pos*8 +: 32] = smp;       pos_n = pos + 4'd4; end
        endcase
    end

    assign fifo_push = push_r;
    assign fifo_din  = push_data;

    // AXI-запись (объявления нужны раньше для условия окончания)
    reg [31:0] pushed_total, reserved_total;
    reg [3:0]  outstanding;
    reg        w_active;
    reg [4:0]  w_left;
    reg [4:0]  lq [0:7];      // очередь длин пачек AW → W
    reg [2:0]  lq_wr, lq_rd;
    reg [3:0]  lq_cnt;
    reg [31:0] woff;
    reg [4:0]  aw_len_r;

    always @(posedge aclk) begin
        push_r <= 1'b0;
        if (!aresetn) begin
            state <= ST_IDLE;
            triggered <= 1'b0; overflow <= 1'b0;
            trigpos <= 32'd0; scount <= 32'd0;
            div_cnt <= 32'd0; have_prev <= 1'b0; prev <= 32'd0;
            stop_at <= 32'd0; pack <= 64'd0; pos <= 4'd0;
            push_data <= 64'd0;
            settle <= 3'd0;
        end else begin
            case (state)
            ST_IDLE, ST_DONE: begin
                if (start_pulse) begin
                    state <= ST_CAPT;
                    triggered <= 1'b0; overflow <= 1'b0;
                    trigpos <= 32'd0; scount <= 32'd0;
                    div_cnt <= 32'd0; have_prev <= 1'b0;
                    stop_at <= 32'd0; pack <= 64'd0; pos <= 4'd0;
                end
            end
            ST_CAPT: begin
                div_cnt <= (div_cnt == 32'd0) ? r_div : div_cnt - 32'd1;
                if (stop_pulse) begin
                    state <= ST_FLUSH;
                end else if (tick) begin
                    prev <= smp;
                    have_prev <= 1'b1;
                    scount <= scount + 32'd1;
                    if (trig_now) begin
                        triggered <= 1'b1;
                        trigpos   <= scount;
                        stop_at   <= stop_at_n;
                    end
                    if (pos_n == 4'd8) begin
                        if (fifo_full) begin
                            overflow <= 1'b1;
                            state <= ST_FLUSH;
                        end
                        push_r    <= !fifo_full;
                        push_data <= pack_n;
                        pack      <= 64'd0;
                        pos       <= 4'd0;
                    end else begin
                        pack <= pack_n;
                        pos  <= pos_n;
                    end
                    if (last_smp)
                        state <= ST_FLUSH;
                end
            end
            ST_FLUSH: begin
                // дописать неполное слово
                if (pos != 4'd0 && !fifo_full) begin
                    push_r    <= 1'b1;
                    push_data <= pack;
                end
                pos    <= 4'd0;
                settle <= 3'd4;
                state  <= ST_DRAIN;
            end
            ST_DRAIN: begin
                // push_r → wr_ptr → wr_ptr_d → pushed_total: даём счётчикам устояться,
                // затем ждём, пока всё уйдёт в DDR
                if (settle != 3'd0)
                    settle <= settle - 3'd1;
                else if ((pushed_total == reserved_total) && (lq_cnt == 4'd0) &&
                    !w_active && (outstanding == 4'd0) && !m_axi_awvalid)
                    state <= ST_DONE;
            end
            default: state <= ST_IDLE;
            endcase
        end
    end

    // -----------------------------------------------------------------
    // Указатели FIFO
    // -----------------------------------------------------------------
    always @(posedge aclk) begin
        if (!aresetn || (start_pulse && (state == ST_IDLE || state == ST_DONE))) begin
            wr_ptr <= 0; wr_ptr_d <= 0; rd_ptr <= 0;
            pushed_total <= 32'd0;
        end else begin
            if (fifo_push && !fifo_full) begin
                wr_ptr <= wr_ptr + 1'b1;
            end
            wr_ptr_d <= wr_ptr;
            rd_ptr   <= rd_ptr_n;
            // pushed_total считается по wr_ptr_d, т.е. только видимые чтению слова
            if (wr_ptr_d != wr_ptr)
                pushed_total <= pushed_total + 32'd1;
        end
    end

    // -----------------------------------------------------------------
    // AXI4-запись: AW (резервирует слова) → очередь длин → W; B считаются
    // -----------------------------------------------------------------
    wire [31:0] unreserved = pushed_total - reserved_total;
    wire        ending     = (state == ST_DRAIN) && (settle == 3'd0);
    wire        aw_can     = !m_axi_awvalid && (outstanding < 4'd8) && (lq_cnt < 4'd6) &&
                             ((unreserved >= 32'd16) || (ending && unreserved != 32'd0));
    wire [4:0]  aw_len_n   = (unreserved >= 32'd16) ? 5'd16 : unreserved[4:0];

    assign m_axi_awaddr  = r_base + woff;
    assign m_axi_awlen   = {4'd0, aw_len_r[3:0] - 4'd1};   // 16 → 15
    assign m_axi_awsize  = 3'd3;      // 8 байт
    assign m_axi_awburst = 2'b01;     // INCR
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot  = 3'b000;

    wire aw_fire = m_axi_awvalid && m_axi_awready;
    wire w_fire  = m_axi_wvalid && m_axi_wready;
    wire b_fire  = m_axi_bvalid && m_axi_bready;
    wire lq_pop  = !w_active && (lq_cnt != 4'd0);

    assign m_axi_wvalid = w_active && fifo_avail;
    assign m_axi_wdata  = fifo_q;
    assign m_axi_wstrb  = 8'hFF;
    assign m_axi_wlast  = (w_left == 5'd1);
    assign m_axi_bready = 1'b1;
    assign fifo_pop     = w_fire;

    always @(posedge aclk) begin
        if (!aresetn || (start_pulse && (state == ST_IDLE || state == ST_DONE))) begin
            m_axi_awvalid <= 1'b0;
            aw_len_r <= 5'd0;
            reserved_total <= 32'd0;
            woff <= 32'd0;
            outstanding <= 4'd0;
            w_active <= 1'b0; w_left <= 5'd0;
            lq_wr <= 3'd0; lq_rd <= 3'd0; lq_cnt <= 4'd0;
            words_done <= 32'd0;
            if (!aresetn) axi_err <= 1'b0;
            if (start_pulse) axi_err <= 1'b0;
        end else begin
            // AW
            if (aw_can) begin
                m_axi_awvalid  <= 1'b1;
                aw_len_r       <= aw_len_n;
                reserved_total <= reserved_total + {27'd0, aw_len_n};
            end else if (aw_fire) begin
                m_axi_awvalid <= 1'b0;
                woff <= (woff + {24'd0, aw_len_r, 3'd0} >= r_size) ? 32'd0
                                                                    : woff + {24'd0, aw_len_r, 3'd0};
            end

            // очередь длин: запись по AW-рукопожатию, чтение при старте W-пачки
            if (aw_fire) begin
                lq[lq_wr] <= aw_len_r;
                lq_wr <= lq_wr + 3'd1;
            end
            lq_cnt <= lq_cnt + (aw_fire ? 4'd1 : 4'd0) - (lq_pop ? 4'd1 : 4'd0);

            // W
            if (lq_pop) begin
                w_active <= 1'b1;
                w_left   <= lq[lq_rd];
                lq_rd    <= lq_rd + 3'd1;
            end else if (w_fire) begin
                w_left <= w_left - 5'd1;
                words_done <= words_done + 32'd1;
                if (w_left == 5'd1)
                    w_active <= 1'b0;
            end

            // B
            outstanding <= outstanding + (aw_fire ? 4'd1 : 4'd0) - (b_fire ? 4'd1 : 4'd0);
            if (b_fire && m_axi_bresp != 2'b00)
                axi_err <= 1'b1;
        end
    end

endmodule

`default_nettype wire
