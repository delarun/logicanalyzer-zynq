// Тестбенч s9probe: петли TDI→TDO, MOSI→MISO, UART TX→RX, SWDIO с подтяжкой
`timescale 1ns / 1ps

module tb_s9probe;
    reg aclk = 0, aresetn = 0;
    always #5 aclk = ~aclk;

    reg  [6:0]  awaddr = 0; reg awvalid = 0; wire awready;
    reg  [31:0] wdata = 0;  reg wvalid = 0;  wire wready;
    wire [1:0]  bresp; wire bvalid; reg bready = 1;
    reg  [6:0]  araddr = 0; reg arvalid = 0; wire arready;
    wire [31:0] rdata; wire [1:0] rresp; wire rvalid; reg rready = 1;

    wire [13:0] po, poe;
    reg  [13:0] pi;
    // петли
    always @(*) begin
        pi = 14'h3FFF;
        pi[3] = po[2];                      // TDO ← TDI
        pi[9] = po[8];                      // MISO ← MOSI
        pi[6] = poe[5] ? po[5] : 1'b1;      // URX ← UTX (подтяжка, пока не выход)
        pi[1] = poe[1] ? po[1] : 1'b1;      // SWDIO: отпущена — подтяжка 1
    end

    s9probe dut (
        .aclk(aclk), .aresetn(aresetn), .pin_o(po), .pin_oe(poe), .pin_i(pi),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready)
    );

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
    task res(output [31:0] d);
        integer g;
        begin
            d = 0; g = 0;
            while (!d[31] && g < 2000) begin rd(7'h1C, d); g = g + 1; end
        end
    endtask

    integer errors = 0, i;
    reg [31:0] v;
    initial begin
        repeat (5) @(posedge aclk); aresetn <= 1; repeat (5) @(posedge aclk);
        rd(7'h00, v); $display("ID %h", v); if (v != 32'h50524231) errors = errors + 1;

        // JTAG: TDI (B) по петле возвращается как TDO
        wr(7'h04, 32'h1); wr(7'h08, 2);
        for (i = 0; i < 8; i = i + 1) begin
            wr(7'h18, {3'b000, 2'd0, 3'd7, 8'h00, 8'h3C ^ i[7:0], 8'h00});
            res(v);
            if (v[7:0] != (8'h3C ^ i[7:0])) begin $display("JTAG %0d: %h", i, v[7:0]); errors = errors + 1; end
        end
        // частичная команда: 5 бит
        wr(7'h18, {3'b000, 2'd0, 3'd4, 8'h00, 8'h15, 8'h00}); res(v);
        if (v[4:0] != 5'h15) begin $display("JTAG 5 бит: %h", v[7:0]); errors = errors + 1; end
        $display("JTAG: ok");

        // SPI: MOSI → MISO
        wr(7'h04, 32'h4);
        wr(7'h18, {3'b000, 2'd2, 3'd7, 8'h00, 8'h00, 8'hA5}); res(v);
        if (v[7:0] != 8'hA5) begin $display("SPI: %h", v[7:0]); errors = errors + 1; end
        // без ответа + с ответом
        wr(7'h18, {3'b001, 2'd2, 3'd7, 8'h00, 8'h00, 8'h11});
        wr(7'h18, {3'b000, 2'd2, 3'd7, 8'h00, 8'h00, 8'h5A}); res(v);
        if (v[7:0] != 8'h5A) begin $display("SPI no-resp: %h", v[7:0]); errors = errors + 1; end
        $display("SPI: ok");

        // SWD: биты 0-3 выводим (AOE=1), 4-7 отпускаем (подтяжка → 1)
        wr(7'h04, 32'h2);
        wr(7'h18, {3'b000, 2'd1, 3'd7, 8'h0F, 8'h00, 8'h05}); res(v);
        if (v[7:0] != 8'hF5) begin $display("SWD: %h", v[7:0]); errors = errors + 1; end
        $display("SWD: ok");

        // UART: 1 Мбод (делитель 100), петля
        wr(7'h04, 32'h8); wr(7'h24, 100);
        wr(7'h28, 8'h55); wr(7'h28, 8'hC3);
        repeat (2500) @(posedge aclk);
        rd(7'h2C, v); if (v[31:30] != 2'b10 || v[7:0] != 8'h55) begin $display("UART1 %h", v); errors = errors + 1; end
        rd(7'h2C, v); if (v[31:30] != 2'b10 || v[7:0] != 8'hC3) begin $display("UART2 %h", v); errors = errors + 1; end
        rd(7'h2C, v); if (v[31]) begin $display("UART лишний байт %h", v); errors = errors + 1; end
        $display("UART: ok");

        // GPIO
        wr(7'h04, 0); wr(7'h0C, 14'h1000); wr(7'h10, 14'h1000);
        repeat (4) @(posedge aclk);
        if (!poe[12] || !po[12]) begin $display("GPIO"); errors = errors + 1; end

        if (errors == 0) $display("PASS"); else $display("FAIL: %0d", errors);
        $finish;
    end
    initial begin #5_000_000; $display("FAIL: таймаут"); $finish; end
endmodule
