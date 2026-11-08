// Тестбенч s9la_core: режим TEST (выборка = её номер), модель памяти AXI со
// случайными задержками ready. Запуск: hw/la/sim/run.sh
`timescale 1ns / 1ps

module tb_s9la_core;
    reg aclk = 0, aresetn = 0;
    always #5 aclk = ~aclk;

    // AXI-Lite
    reg  [6:0]  awaddr = 0; reg awvalid = 0; wire awready;
    reg  [31:0] wdata = 0;  reg wvalid = 0;  wire wready;
    wire [1:0]  bresp; wire bvalid; reg bready = 1;
    reg  [6:0]  araddr = 0; reg arvalid = 0; wire arready;
    wire [31:0] rdata; wire [1:0] rresp; wire rvalid; reg rready = 1;

    // AXI master
    wire [31:0] m_awaddr; wire [7:0] m_awlen; wire [2:0] m_awsize; wire [1:0] m_awburst;
    wire [3:0]  m_awcache; wire [2:0] m_awprot; wire m_awvalid; reg m_awready = 0;
    wire [63:0] m_wdata; wire [7:0] m_wstrb; wire m_wlast; wire m_wvalid; reg m_wready = 0;
    reg  [1:0]  m_bresp = 0; reg m_bvalid = 0; wire m_bready;

    wire [3:0] led;

    s9la_core #(.USE_DNA(0)) dut (
        .aclk(aclk), .aresetn(aresetn),
        .hb_rx(8'd0), .hb_tx(8'd0), .hb_rst(8'd0), .hb_plug(8'd0),
        .fan_pwm(), .led(led),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .m_axi_awaddr(m_awaddr), .m_axi_awlen(m_awlen), .m_axi_awsize(m_awsize),
        .m_axi_awburst(m_awburst), .m_axi_awcache(m_awcache), .m_axi_awprot(m_awprot),
        .m_axi_awvalid(m_awvalid), .m_axi_awready(m_awready),
        .m_axi_wdata(m_wdata), .m_axi_wstrb(m_wstrb), .m_axi_wlast(m_wlast),
        .m_axi_wvalid(m_wvalid), .m_axi_wready(m_wready),
        .m_axi_bresp(m_bresp), .m_axi_bvalid(m_bvalid), .m_axi_bready(m_bready)
    );

    // ---------------- модель памяти ----------------
    localparam MEM_WORDS = 16384;           // 128 КБ с адреса 0
    reg [63:0] mem [0:MEM_WORDS-1];
    reg [31:0] aq_addr [0:63]; reg [7:0] aq_len [0:63];
    integer aq_wr = 0, aq_rd = 0, pend_b = 0;
    integer cur_beat = 0, errors = 0;
    integer rnd;

    always @(posedge aclk) begin
        rnd = $random;
        m_awready <= rnd[0] | rnd[1];
        m_wready  <= rnd[2] | rnd[3] | rnd[4];
        if (m_awvalid && m_awready) begin
            if (m_awaddr[11:0] + (m_awlen + 1) * 8 > 4096) begin
                $display("ОШИБКА: пачка пересекает 4 КБ: %h len %0d", m_awaddr, m_awlen);
                errors = errors + 1;
            end
            aq_addr[aq_wr % 64] = m_awaddr; aq_len[aq_wr % 64] = m_awlen; aq_wr = aq_wr + 1;
        end
        if (m_wvalid && m_wready) begin
            if (aq_rd >= aq_wr) begin
                $display("ОШИБКА: W раньше AW"); errors = errors + 1;
            end else begin
                mem[(aq_addr[aq_rd % 64] >> 3) + cur_beat] = m_wdata;
                if (m_wlast != (cur_beat == aq_len[aq_rd % 64])) begin
                    $display("ОШИБКА: WLAST на такте %0d из %0d", cur_beat, aq_len[aq_rd % 64]);
                    errors = errors + 1;
                end
                if (cur_beat == aq_len[aq_rd % 64]) begin
                    cur_beat = 0; aq_rd = aq_rd + 1; pend_b = pend_b + 1;
                end else cur_beat = cur_beat + 1;
            end
        end
        if (m_bvalid && m_bready) begin m_bvalid <= 0; pend_b = pend_b - 1; end
        else if (pend_b > 0 && rnd[5]) m_bvalid <= 1;
    end

    // ---------------- AXI-Lite ----------------
    task wr(input [6:0] a, input [31:0] d);
        begin
            @(posedge aclk); awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
            @(posedge aclk); while (!awready) @(posedge aclk);
            awvalid <= 0; wvalid <= 0;
            while (!bvalid) @(posedge aclk);
            @(posedge aclk);
        end
    endtask

    task rd(input [6:0] a, output [31:0] d);
        begin
            @(posedge aclk); araddr <= a; arvalid <= 1;
            @(posedge aclk); while (!arready) @(posedge aclk);
            arvalid <= 0;
            while (!rvalid) @(posedge aclk);
            d = rdata;
            @(posedge aclk);
        end
    endtask

    task wait_done;
        reg [31:0] st; integer guard;
        begin
            guard = 0; st = 0;
            while (st[2:0] != 3'd4 && guard < 200000) begin rd(7'h04, st); guard = guard + 1; end
            if (st[2:0] != 3'd4) begin $display("ОШИБКА: захват не завершился"); errors = errors + 1; end
            if (st[5]) begin $display("ОШИБКА: OVERFLOW"); errors = errors + 1; end
        end
    endtask

    // выборка k из кольца: base, size, unit
    function [31:0] sample_at(input [31:0] base, input [31:0] size, input integer unit_b, input [31:0] k);
        reg [31:0] off; reg [63:0] w; integer sh;
        begin
            off = base + (k * unit_b) % size;
            w = mem[off >> 3];
            sh = (off & 7) * 8;
            sample_at = (w >> sh) & ((unit_b == 4) ? 64'hFFFF_FFFF : (unit_b == 2) ? 64'hFFFF : 64'hFF);
        end
    endfunction

    task check_range(input [31:0] base, input [31:0] size, input integer unit_b,
                     input [31:0] first, input [31:0] last);
        reg [31:0] k, got, exp; integer bad;
        begin
            bad = 0;
            for (k = first; k <= last; k = k + 1) begin
                got = sample_at(base, size, unit_b, k);
                exp = k & ((unit_b == 4) ? 32'hFFFF_FFFF : (unit_b == 2) ? 32'hFFFF : 32'hFF);
                if (got !== exp) begin
                    if (bad < 5) $display("  выборка %0d: %h, ожидалось %h", k, got, exp);
                    bad = bad + 1;
                end
            end
            if (bad) begin $display("ОШИБКА: %0d несовпадений", bad); errors = errors + bad; end
        end
    endtask

    reg [31:0] v, cnt, tp, words;
    integer i;
    initial begin
        for (i = 0; i < MEM_WORDS; i = i + 1) mem[i] = 64'hDEAD_BEEF_DEAD_BEEF;
        repeat (5) @(posedge aclk);
        aresetn <= 1;
        repeat (5) @(posedge aclk);

        rd(7'h38, v);
        $display("ID = %h", v);
        if (v != 32'h53394C41) errors = errors + 1;

        // 1) 32-битные выборки, без триггера, 1000 выборок (последнее слово неполное — 1001 нет)
        $display("Тест 1: unit=4 байта, div=0, 1001 выборка, кольцо 64 КБ");
        wr(7'h08, 0); wr(7'h0C, 2); wr(7'h10, 32'h0); wr(7'h14, 32'h10000);
        wr(7'h18, 0); wr(7'h1C, 1001);
        wr(7'h20, 0); wr(7'h24, 0); wr(7'h28, 0); wr(7'h2C, 0);
        wr(7'h00, 32'h11);              // TEST + START
        wait_done;
        rd(7'h34, cnt); rd(7'h30, tp); rd(7'h44, words);
        $display("  count=%0d trigpos=%0d words=%0d", cnt, tp, words);
        if (cnt != 1001 || tp != 0 || words != 501) errors = errors + 1;
        check_range(0, 32'h10000, 4, 0, 1000);

        // 2) 1-байтные выборки, кольцо 1 КБ, триггер на значении 3000 (младшие 16 бит)
        $display("Тест 2: unit=1 байт, кольцо 1 КБ с адреса 0x8000, триггер = 3000, pre 100, post 300");
        wr(7'h0C, 0); wr(7'h10, 32'h8000); wr(7'h14, 1024);
        wr(7'h18, 100); wr(7'h1C, 300);
        wr(7'h20, 32'hFFFF); wr(7'h24, 3000);
        wr(7'h00, 32'h11);
        wait_done;
        rd(7'h34, cnt); rd(7'h30, tp); rd(7'h04, v);
        $display("  count=%0d trigpos=%0d triggered=%0d", cnt, tp, v[4]);
        if (tp != 3000 || cnt != 3300 || !v[4]) errors = errors + 1;
        check_range(32'h8000, 1024, 1, tp - 100, tp + 299);

        // 3) 2-байтные выборки, делитель 3, фронт на бите 4, остановка по STOP
        $display("Тест 3: unit=2 байта, div=3, фронт бита 4, остановка STOP");
        wr(7'h08, 3); wr(7'h0C, 1); wr(7'h10, 32'h4000); wr(7'h14, 32'h4000);
        wr(7'h18, 10); wr(7'h1C, 32'hFFFF_FFFF);
        wr(7'h20, 0); wr(7'h24, 0); wr(7'h28, 32'h10); wr(7'h2C, 0);
        wr(7'h00, 32'h11);
        repeat (3000) @(posedge aclk);
        wr(7'h00, 32'h12);              // TEST + STOP
        wait_done;
        rd(7'h34, cnt); rd(7'h30, tp); rd(7'h04, v);
        $display("  count=%0d trigpos=%0d triggered=%0d", cnt, tp, v[4]);
        // первый фронт бита 4 после 10 выборок: номер 16
        if (tp != 16 || !v[4] || cnt < 700) errors = errors + 1;
        check_range(32'h4000, 32'h4000, 2, (cnt > 8192) ? cnt - 8192 : 0, cnt - 1);

        if (errors == 0) $display("PASS");
        else $display("FAIL: ошибок %0d", errors);
        $finish;
    end

    initial begin
        #20_000_000;
        $display("FAIL: таймаут");
        $finish;
    end
endmodule
