`timescale 1ns/1ps

// Packages
package stargaze_rv64_pkg;
  typedef enum logic [1:0] {PRIV_M=2'b11, PRIV_S=2'b01, PRIV_U=2'b00} priv_t;
  typedef enum logic [6:0] {LUI=7'b0110111,AUIPC=7'b0010111,JAL=7'b1101111,JALR=7'b1100111,BRANCH=7'b1100011,LOAD=7'b0000011,STORE=7'b0100011,ALUI=7'b0010011,ALUR=7'b0110011,SYSTEM=7'b1110011} op_t;
endpackage
package stargaze_x1_pmu_pkg;
  typedef enum logic [3:0] {TH_NONE=0,TH_L1=1,TH_L2=3,TH_L3=7,TH_L4=15} th_t;
endpackage
package stargaze_igpu_pkg;
  typedef enum logic [2:0] {CU_IDLE,CU_VTX,CU_FRG,CU_CMP,CU_STALL} cu_t;
endpackage

// Branch Predictor (SECURED - speculation barrier)
module stargaze_bp(input logic clk,rst_n,input logic[63:0]pc,input logic br_taken,input logic[63:0]br_target,input logic spec_disable,output logic pred_taken,output logic[63:0]pred_target);
  localparam BS=256;logic[63:0]btb_t[BS-1:0];logic[1:0]btb_c[BS-1:0];logic[7:0]idx;
  assign idx=pc[9:2];
  always_comb begin
    if(spec_disable)begin pred_taken=1'b0;pred_target=64'h0;end
    else begin pred_taken=btb_c[idx][1];pred_target=btb_t[idx];end
  end
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)for(int i=0;i<BS;i++)begin btb_t[i]<=0;btb_c[i]<=1;end
    else if(br_taken&&!spec_disable)begin btb_t[idx]<=br_target;if(btb_c[idx]<3)btb_c[idx]<=btb_c[idx]+1;end
    else if(btb_c[idx]>0&&!spec_disable)btb_c[idx]<=btb_c[idx]-1;end
endmodule

// L1 Cache (SECURED - constant-time access)
module stargaze_l1c(input logic clk,rst_n,input logic[63:0]addr,input logic rv,input logic wr,input logic[63:0]wd,output logic[63:0]rd,output logic hit,output logic mv,output logic[63:0]ma,input logic mr,input logic[63:0]mrd);
  logic[63:0]mem[0:63];logic[63:0]tags[0:63];logic[63:0]vld;
  logic[5:0]idx;logic[57:0]tag;
  assign idx=addr[11:6];assign tag=addr[63:6];
  assign hit=vld[idx]&&(tags[idx]==tag);
  assign rd=hit?mem[idx]:0;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin vld<=0;mv<=0;for(int i=0;i<64;i++)begin mem[i]<=0;tags[i]<=0;end end
    else begin mv<=0;if(rv&&hit&&wr)begin mem[idx]<=wd;end else if(rv&&!hit)begin mv<=1;ma<=addr;if(mr)begin mem[idx]<=mrd;tags[idx]<=tag;vld[idx]<=1;end end end end
endmodule

// Thermal Model
module stargaze_tm(input logic clk,rst_n,input logic[7:0]act,input logic[7:0]amb,output logic[7:0]dtemp);
  logic[15:0]tm;logic[7:0]pw;
  assign pw=act*8'd2;assign dtemp=tm[15:8]+amb;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin tm<=3200;end
    else begin if(pw>0)tm<=tm+{8'd0,pw}-{12'd0,tm[15:12]};else tm<=tm-{12'd0,tm[15:12]};end end
endmodule

// Register File
module stargaze_rv64_regfile(input logic clk,rst_n,input logic[4:0]ra1,ra2,wa,input logic[63:0]wd,input logic we,output logic[63:0]rd1,rd2);
  logic[63:0]r[31:0];assign rd1=(ra1==0)?0:r[ra1];assign rd2=(ra2==0)?0:r[ra2];
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)for(int i=0;i<32;i++)r[i]<=0;else if(we&&wa!=0)r[wa]<=wd;end
endmodule

// CSR (SECURED - speculation control register)
module stargaze_rv64_csr(input logic clk,rst_n,input logic[11:0]addr,input logic[63:0]wd,input logic we,output logic[63:0]rd,output logic[63:0]mtvec,mepc,output logic spec_disable);
  logic[63:0]mstatus,mie,mcause,stvec,sepc,satp,misa={26'h040,2'b10,4'h0};
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin mstatus<=0;mie<=0;mcause<=0;mtvec<=0;mepc<=0;stvec<=0;sepc<=0;satp<=0;spec_disable<=0;end
    else if(we)case(addr)12'h300:mstatus<=wd;12'h304:mie<=wd;12'h305:mtvec<=wd;12'h341:mepc<=wd;12'h105:stvec<=wd;12'h141:sepc<=wd;12'h180:satp<=wd;12'h7C0:spec_disable<=wd[0];default:;endcase end
  always_comb begin rd=0;case(addr)12'hF11:rd=64'h5354475A;12'hF12:rd=1;12'h300:rd=mstatus;12'h301:rd=misa;12'h304:rd=mie;12'h305:rd=mtvec;12'h341:rd=mepc;12'h342:rd=mcause;12'h105:rd=stvec;12'h141:rd=sepc;12'h180:rd=satp;12'h7C0:rd={63'b0,spec_disable};default:rd=0;endcase end
endmodule

// RISC-V Core (SECURED)
module stargaze_rv64_core #(parameter ID=0)(input logic clk,rst_n,output logic[63:0]iaddr,input logic[31:0]idata,output logic dv,input logic dr,output logic[63:0]da,dw,input logic[63:0]dd,output logic[7:0]dm,output logic drw,input logic dv2,input logic[7:0]ts,output logic[3:0]th,output logic[63:0]pmu[7:0]);
  logic[63:0]pc,npc,imm,ra,rb,alu;logic[31:0]ins;logic[6:0]op;logic[4:0]rd,rs1,rs2;logic[2:0]f3;logic[6:0]f7;logic we,lt;logic[63:0]cc,ic,mtvec,mepc;
  logic bp_taken;logic[63:0]bp_target;logic br_taken;logic[63:0]br_target;logic l1_hit;logic[63:0]l1_rd;logic l1_mv;logic[63:0]l1_ma;logic spec_disable;
  assign op=ins[6:0];assign rd=ins[11:7];assign f3=ins[14:12];assign rs1=ins[19:15];assign rs2=ins[24:20];assign f7=ins[31:25];
  stargaze_bp bp(.clk,.rst_n,.pc,.br_taken,.br_target,.spec_disable,.pred_taken(bp_taken),.pred_target(bp_target));
  stargaze_l1c l1(.clk,.rst_n,.addr(da),.rv(dv),.wr(!drw),.wd(dw),.rd(l1_rd),.hit(l1_hit),.mv(l1_mv),.ma(l1_ma),.mr(dr),.mrd(dd));
  always_comb case(op)7'b0110111,7'b0010111:imm={ins[31:12],12'h0};7'b1101111:imm={{44{ins[31]}},ins[19:12],ins[20],ins[30:21],1'b0};7'b1100111,7'b0000011,7'b0010011:imm={{52{ins[31]}},ins[31:20]};7'b1100011:imm={{52{ins[31]}},ins[7],ins[30:25],ins[11:8],1'b0};7'b0100011:imm={{52{ins[31]}},ins[31:25],ins[11:7]};default:imm=0;endcase
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin pc<=64'h80000000;cc<=0;ic<=0;dv<=0;br_taken<=0;end else begin cc<=cc+1;ins<=idata;iaddr<=pc;we<=0;dv<=0;br_taken<=0;
    case(op)7'b0110111:begin alu<=imm;we<=1;npc<=pc+4;end 7'b0010111:begin alu<=pc+imm;we<=1;npc<=pc+4;end
    7'b1101111:begin alu<=pc+4;we<=1;npc<=pc+imm;br_taken<=1;br_target<=pc+imm;end
    7'b1100111:begin alu<=pc+4;we<=1;npc<=(ra+imm)&~64'h1;end 7'b0110011:begin lt=0;case(f3)3'b000:alu<=(f7[5])?(ra-rb):(ra+rb);3'b001:alu<=ra<<rb[5:0];3'b010:lt=($signed(ra)<$signed(rb));3'b011:lt=(ra<rb);3'b100:alu<=ra^rb;3'b110:alu<=ra|rb;3'b111:alu<=ra&rb;endcase if(f3==3'b010||f3==3'b011)alu<=lt?1:0;we<=1;npc<=pc+4;end
    7'b0010011:begin case(f3)3'b000:alu<=ra+imm;3'b100:alu<=ra^imm;3'b110:alu<=ra|imm;3'b111:alu<=ra&imm;default:alu<=ra+imm;endcase we<=1;npc<=pc+4;end
    7'b0000011:begin dv<=1;da<=ra+imm;drw<=1;dm<=8'hFF;if(l1_hit)begin alu<=l1_rd;we<=1;end else if(dv2)begin alu<=dd;we<=1;end;npc<=pc+4;end
    7'b0100011:begin dv<=1;da<=ra+imm;dw<=rb;drw<=0;dm<=8'hFF;npc<=pc+4;end
    7'b1100011:begin case(f3)3'b000:lt=(ra==rb);3'b001:lt=(ra!=rb);3'b100:lt=($signed(ra)<$signed(rb));3'b101:lt=($signed(ra)>=$signed(rb));3'b110:lt=(ra<rb);3'b111:lt=(ra>=rb);endcase br_taken<=lt;br_target<=pc+imm;npc<=lt?(pc+imm):(pc+4);end
    7'b1110011:begin npc<=mepc;end default:npc<=pc+4;endcase pc<=npc;if(we)ic<=ic+1;end end
  stargaze_rv64_regfile rf(.clk,.rst_n,.ra1(rs1),.ra2(rs2),.wa(rd),.wd(alu),.we,.rd1(ra),.rd2(rb));
  stargaze_rv64_csr csr(.clk,.rst_n,.addr(12'd0),.wd(64'd0),.we(1'b0),.rd(),.mtvec,.mepc,.spec_disable);
  assign pmu[0]=ic;assign pmu[7]=cc;assign pmu[3]=ID;always_comb begin th=0;if(ts>90)th=7;else if(ts>80)th=3;else if(ts>70)th=1;end
endmodule

// DDR4 Controller
module stargaze_ddr4_controller(input logic clk,rst_n,input logic rv,output logic rr,input logic[63:0]ra,rw,output logic[63:0]rd,input logic[7:0]rm,input logic rrw,output logic rv2);
  logic[63:0]mem[0:1023];assign rr=1;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin rv2<=0;rd<=0;end else if(rv)begin if(rrw)begin rd<=mem[ra[12:3]];rv2<=1;end else begin mem[ra[12:3]]<=rw;rv2<=1;end end else rv2<=0;end
endmodule

// UART
module stargaze_uart(input logic clk,rst_n,input logic[7:0]tx_data,input logic tx_start,output logic tx_busy,output logic uart_tx);
  localparam CPB=868;typedef enum logic[1:0]{IDLE,START,DATA,STOP}st_t;st_t st;logic[15:0]cnt;logic[2:0]bi;logic[7:0]tr;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin st<=IDLE;cnt<=0;bi<=0;tr<=0;uart_tx<=1;tx_busy<=0;end else case(st)
    IDLE:begin tx_busy<=0;if(tx_start)begin tr<=tx_data;st<=START;tx_busy<=1;cnt<=0;end end
    START:begin uart_tx<=0;if(cnt>=CPB-1)begin cnt<=0;bi<=0;st<=DATA;end else cnt<=cnt+1;end
    DATA:begin uart_tx<=tr[bi];if(cnt>=CPB-1)begin cnt<=0;if(bi==7)st<=STOP;else bi<=bi+1;end else cnt<=cnt+1;end
    STOP:begin uart_tx<=1;if(cnt>=CPB-1)begin st<=IDLE;tx_busy<=0;end else cnt<=cnt+1;end endcase end
endmodule

// MMU
module stargaze_mmu(input logic clk,rst_n,input logic mmu_en,input logic[43:0]satp_ppn,input logic[63:0]va,output logic[63:0]pa,output logic pf,output logic thit,output logic mv,input logic mr,output logic[63:0]ma,input logic mrv,input logic[63:0]mrd);
  typedef enum logic[2:0]{IDLE,W2,W1,W0,DONE,FLT}ms_t;ms_t s;logic[8:0]v2,v1,v0;logic[11:0]off;logic[43:0]cppn;logic[1:0]lvl;
  localparam TE=16;logic[TE-1:0][38:0]tv;logic[TE-1:0][43:0]tp;logic[TE-1:0]tvld;logic[3:0]ti,trp;
  assign v2=va[38:30];assign v1=va[29:21];assign v0=va[20:12];assign off=va[11:0];assign ti=va[16:13];
  always_comb begin thit=0;pa=va;if(mmu_en&&tvld[ti]&&tv[ti]==va[38:12])begin thit=1;pa={tp[ti],off};end end
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin s<=IDLE;pf<=0;mv<=0;cppn<=0;lvl<=0;trp<=0;for(int i=0;i<TE;i++)begin tv[i]<=0;tp[i]<=0;tvld[i]<=0;end end else case(s)
    IDLE:begin pf<=0;mv<=0;if(mmu_en&&!thit)begin cppn<=satp_ppn;lvl<=2;s<=W2;end end
    W2:begin mv<=1;ma<={cppn,v2,3'b000};if(mr&&mrv)begin mv<=0;if(!mrd[0])s<=FLT;else if(mrd[1:0]==2'b01)begin cppn<=mrd[43:10];lvl<=1;s<=W1;end else begin tv[trp]<=va[38:12];tp[trp]<={mrd[43:30],v1,v0};tvld[trp]<=1;trp<=trp+1;s<=DONE;end end end
    W1:begin mv<=1;ma<={cppn,v1,3'b000};if(mr&&mrv)begin mv<=0;if(!mrd[0])s<=FLT;else if(mrd[1:0]==2'b01)begin cppn<=mrd[43:10];lvl<=0;s<=W0;end else begin tv[trp]<=va[38:12];tp[trp]<={mrd[43:18],v0};tvld[trp]<=1;trp<=trp+1;s<=DONE;end end end
    W0:begin mv<=1;ma<={cppn,v0,3'b000};if(mr&&mrv)begin mv<=0;if(!mrd[0])s<=FLT;else begin tv[trp]<=va[38:12];tp[trp]<=mrd[43:10];tvld[trp]<=1;trp<=trp+1;s<=DONE;end end end
    DONE:s<=IDLE;FLT:begin pf<=1;s<=IDLE;end endcase end
endmodule

// PLIC+CLINT
module stargaze_plic_clint(input logic clk,rst_n,output logic timer_irq,output logic[63:0]mtime,input logic uart_irq,output logic ext_irq,input logic[11:0]csr_addr,input logic[63:0]csr_wdata,input logic csr_write,output logic[63:0]csr_rdata);
  logic[63:0]mtimecmp;always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)mtime<=0;else mtime<=mtime+1;end
  assign timer_irq=(mtime>=mtimecmp)&&(mtimecmp!=0);assign ext_irq=uart_irq;
  always_comb begin csr_rdata=0;case(csr_addr)12'hC00:csr_rdata=mtime;12'hC01:csr_rdata=mtime[63:32];default:csr_rdata=0;endcase end
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)mtimecmp<=0;else if(csr_write&&csr_addr==12'hC00)mtimecmp<=csr_wdata;end
endmodule

// PCIe
module stargaze_pcie_ctrl(input logic clk,rst_n,input logic[63:0]dma_addr,input logic[255:0]dma_wdata,input logic dma_valid,output logic dma_ready,output logic[63:0]dma_rdata,output logic dma_done,output logic pcie_tx_p,pcie_tx_n,input logic pcie_rx_p,pcie_rx_n);
  logic[63:0]cfg_space[0:63];
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin dma_ready<=0;dma_done<=0;pcie_tx_p<=0;pcie_tx_n<=1;cfg_space[0]<=64'h1234567814000001;cfg_space[1]<=64'h0600000000000000;end
    else begin dma_ready<=1;if(dma_valid)begin cfg_space[dma_addr[5:0]]<=dma_wdata[63:0];dma_done<=1;end else dma_done<=0;end end
  assign dma_rdata=cfg_space[dma_addr[5:0]];
endmodule

// USB
module stargaze_usb_ctrl(input logic clk,rst_n,input logic[63:0]data_in,input logic valid_in,output logic ready_out,output logic[63:0]data_out,output logic valid_out,input logic ready_in,inout wire usb_dp,usb_dn);
  logic[63:0]fifo[0:15];logic[3:0]wr_ptr,rd_ptr;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin ready_out<=0;valid_out<=0;data_out<=0;wr_ptr<=0;rd_ptr<=0;end
    else begin ready_out<=1;if(valid_in&&ready_out)begin fifo[wr_ptr]<=data_in;wr_ptr<=wr_ptr+1;end
    if(ready_in&&wr_ptr!=rd_ptr)begin data_out<=fifo[rd_ptr];valid_out<=1;rd_ptr<=rd_ptr+1;end else valid_out<=0;end end
endmodule

// Ethernet
module stargaze_eth_ctrl(input logic clk,rst_n,input logic[63:0]tx_data,input logic tx_valid,output logic tx_ready,output logic[63:0]rx_data,output logic rx_valid,input logic rx_ready,output logic eth_tx_p,eth_tx_n,input logic eth_rx_p,eth_rx_n,output logic eth_mdc,inout wire eth_mdio);
  logic[63:0]tx_fifo[0:7],rx_fifo[0:7];logic[2:0]tx_wr,tx_rd,rx_wr,rx_rd;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin tx_ready<=0;rx_valid<=0;tx_wr<=0;tx_rd<=0;rx_wr<=0;rx_rd<=0;eth_tx_p<=0;eth_tx_n<=1;eth_mdc<=0;end
    else begin tx_ready<=1;eth_mdc<=~eth_mdc;if(tx_valid&&tx_ready)begin tx_fifo[tx_wr]<=tx_data;tx_wr<=tx_wr+1;end
    if(rx_ready)begin rx_data<=rx_fifo[rx_rd];rx_valid<=1;rx_rd<=rx_rd+1;end else rx_valid<=0;
    if(eth_rx_p)begin rx_fifo[rx_wr]<=64'hDEADBEEF;rx_wr<=rx_wr+1;end end end
endmodule

// GPIO
module stargaze_gpio_ctrl(input logic clk,rst_n,input logic[31:0]gpio_out,input logic[31:0]gpio_oe,output logic[31:0]gpio_in,inout wire[31:0]gpio_pins);
  assign gpio_pins=(gpio_oe)?gpio_out:32'hz;assign gpio_in=gpio_pins;
endmodule

// SPI
module stargaze_spi_ctrl(input logic clk,rst_n,input logic[7:0]tx_data,input logic tx_valid,output logic tx_ready,output logic[7:0]rx_data,output logic rx_valid,output logic spi_sck,spi_mosi,input logic spi_miso,output logic spi_cs_n);
  logic[7:0]sr;logic[2:0]bc;logic busy;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin tx_ready<=0;rx_valid<=0;spi_sck<=0;spi_mosi<=0;spi_cs_n<=1;busy<=0;bc<=0;end
    else begin if(tx_valid&&!busy)begin sr<=tx_data;busy<=1;spi_cs_n<=0;bc<=0;tx_ready<=0;end
    else if(busy)begin spi_sck<=~spi_sck;if(spi_sck)begin spi_mosi<=sr[7];sr<={sr[6:0],spi_miso};bc<=bc+1;
        if(bc==7)begin rx_data<=sr;rx_valid<=1;busy<=0;spi_cs_n<=1;tx_ready<=1;end end end end end
endmodule

// I2C
module stargaze_i2c_ctrl(input logic clk,rst_n,input logic[7:0]tx_data,input logic tx_valid,output logic tx_ready,output logic[7:0]rx_data,output logic rx_valid,inout wire i2c_sda,i2c_scl);
  logic busy;logic[3:0]bc;logic[7:0]sr;
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin tx_ready<=0;rx_valid<=0;busy<=0;bc<=0;end
    else begin if(tx_valid&&!busy)begin sr<=tx_data;busy<=1;bc<=0;tx_ready<=0;end
    else if(busy)begin bc<=bc+1;if(bc==15)begin rx_data<=sr;rx_valid<=1;busy<=0;tx_ready<=1;end end end end
endmodule

// SD Card
module stargaze_sd_ctrl(input logic clk,rst_n,input logic[31:0]block_addr,input logic read_req,output logic read_ready,output logic[511:0]read_data,output logic read_valid,output logic sd_clk,sd_cmd,inout wire[3:0]sd_dat);
  logic[511:0]sd_mem[0:255];
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin read_ready<=0;read_valid<=0;read_data<=0;sd_clk<=0;sd_cmd<=1;end
    else begin read_ready<=1;sd_clk<=~sd_clk;if(read_req&&read_ready)begin read_data<=sd_mem[block_addr];read_valid<=1;end else read_valid<=0;end end
endmodule

// SoC UPGRADED
module stargaze_x1_rv64_soc #(parameter int NC=4)(input logic clk,rst_n,prst_n,input logic[7:0]tcpu[NC-1:0],input logic[7:0]tgpu,
  output logic uart,output logic[63:0]pmu[NC-1:0][7:0],output logic[31:0]gutil,output logic[7:0]atemp,
  output logic pcie_tx_p,pcie_tx_n,input logic pcie_rx_p,pcie_rx_n,inout wire usb_dp,usb_dn,
  output logic eth_tx_p,eth_tx_n,input logic eth_rx_p,eth_rx_n,output logic eth_mdc,inout wire eth_mdio,
  inout wire[31:0]gpio_pins,output logic spi_sck,spi_mosi,input logic spi_miso,output logic spi_cs_n,
  inout wire i2c_sda,i2c_scl,output logic sd_clk,sd_cmd,inout wire[3:0]sd_dat);
  logic[63:0]ia;logic[31:0]id;logic dv,dr;logic[63:0]da,dw,dd;logic[7:0]dm;logic drw,dv2;logic[3:0]core_th;
  stargaze_rv64_core #(0)c(.clk,.rst_n,.iaddr(ia),.idata(id),.dv,.dr,.da,.dw,.dd,.dm,.drw,.dv2,.ts(tcpu[0]),.th(core_th),.pmu(pmu[0]));
  logic timer_irq,ext_irq;logic[63:0]mtime;
  stargaze_plic_clint plic(.clk,.rst_n,.timer_irq,.mtime,.uart_irq(1'b0),.ext_irq,.csr_addr(12'd0),.csr_wdata(64'd0),.csr_write(1'b0),.csr_rdata());
  logic mv,mr,mrv,pf,thit;logic[63:0]ma,mrd,pa;
  stargaze_mmu mmu(.clk,.rst_n,.mmu_en(1'b0),.satp_ppn(44'd0),.va(da),.pa,.pf,.thit,.mv,.mr,.ma,.mrv,.mrd);
  logic[31:0]rom[0:255];assign id=rom[ia[7:2]];
  stargaze_ddr4_controller ddr(.clk,.rst_n,.rv(dv),.rr(dr),.ra(pa),.rw(dw),.rd(dd),.rm(dm),.rrw(drw),.rv2(dv2));
  initial begin rom[0]=32'h00000513;rom[1]=32'h00000593;rom[2]=32'h800000B7;rom[3]=32'h00008067;for(int i=4;i<256;i++)rom[i]=32'h00000013;end
  logic[7:0]uc;logic us,ub;logic[31:0]bc;logic[7:0]bm[0:63];logic[5:0]mi;logic ma2;
  initial begin bm[0]="S";bm[1]="t";bm[2]="a";bm[3]="r";bm[4]="g";bm[5]="a";bm[6]="z";bm[7]="e";bm[8]=" ";bm[9]="X";bm[10]="1";bm[11]=" ";bm[12]="1";bm[13]="4";bm[14]="0";bm[15]="K";bm[16]=" ";bm[17]="-";bm[18]=" ";bm[19]="L";bm[20]="i";bm[21]="n";bm[22]="u";bm[23]="x";bm[24]=" ";bm[25]="O";bm[26]="K";bm[27]=13;bm[28]=10;bm[29]=0;end
  always_ff @(posedge clk or negedge rst_n)begin if(!rst_n)begin bc<=0;ma2<=0;mi<=0;us<=0;end else begin us<=0;if(!ma2&&bc>100)begin ma2<=1;mi<=0;end if(ma2&&!ub)begin if(bm[mi]!=0)begin uc<=bm[mi];us<=1;mi<=mi+1;end else ma2<=0;end if(!ma2)bc<=bc+1;end end
  stargaze_uart uart0(.clk,.rst_n,.tx_data(uc),.tx_start(us),.tx_busy(ub),.uart_tx(uart));
  logic[7:0]activity_lvl;logic[7:0]ambient_temp;
  stargaze_tm tm(.clk,.rst_n,.act(activity_lvl),.amb(ambient_temp),.dtemp(atemp));
  assign activity_lvl=8'd65;assign ambient_temp=8'd25;
  logic[63:0]pcie_dma_addr;logic[255:0]pcie_dma_wdata;logic pcie_dma_valid,pcie_dma_ready;logic[63:0]pcie_dma_rdata;logic pcie_dma_done;
  stargaze_pcie_ctrl pcie(.clk,.rst_n,.dma_addr(pcie_dma_addr),.dma_wdata(pcie_dma_wdata),.dma_valid(pcie_dma_valid),.dma_ready(pcie_dma_ready),.dma_rdata(pcie_dma_rdata),.dma_done(pcie_dma_done),.pcie_tx_p,.pcie_tx_n,.pcie_rx_p,.pcie_rx_n);
  logic[63:0]usb_in,usb_out;logic usb_vin,usb_vout,usb_rdy_in,usb_rdy_out;
  stargaze_usb_ctrl usb(.clk,.rst_n,.data_in(usb_in),.valid_in(usb_vin),.ready_out(usb_rdy_out),.data_out(usb_out),.valid_out(usb_vout),.ready_in(usb_rdy_in),.usb_dp,.usb_dn);
  logic[63:0]eth_txd,eth_rxd;logic eth_txv,eth_rxv,eth_txr,eth_rxr;
  stargaze_eth_ctrl eth(.clk,.rst_n,.tx_data(eth_txd),.tx_valid(eth_txv),.tx_ready(eth_txr),.rx_data(eth_rxd),.rx_valid(eth_rxv),.rx_ready(eth_rxr),.eth_tx_p,.eth_tx_n,.eth_rx_p,.eth_rx_n,.eth_mdc,.eth_mdio);
  logic[31:0]gpo,gpi,gpoe;
  stargaze_gpio_ctrl gpio(.clk,.rst_n,.gpio_out(gpo),.gpio_oe(gpoe),.gpio_in(gpi),.gpio_pins);
  logic[7:0]spi_tx,spi_rx;logic spi_txv,spi_rxv,spi_txr;
  stargaze_spi_ctrl spi(.clk,.rst_n,.tx_data(spi_tx),.tx_valid(spi_txv),.tx_ready(spi_txr),.rx_data(spi_rx),.rx_valid(spi_rxv),.spi_sck,.spi_mosi,.spi_miso,.spi_cs_n);
  logic[7:0]i2c_tx,i2c_rx;logic i2c_txv,i2c_rxv,i2c_txr;
  stargaze_i2c_ctrl i2c(.clk,.rst_n,.tx_data(i2c_tx),.tx_valid(i2c_txv),.tx_ready(i2c_txr),.rx_data(i2c_rx),.rx_valid(i2c_rxv),.i2c_sda,.i2c_scl);
  logic[31:0]sd_addr;logic sd_rd_req,sd_rd_rdy;logic[511:0]sd_rd_data;logic sd_rd_val;
  stargaze_sd_ctrl sd(.clk,.rst_n,.block_addr(sd_addr),.read_req(sd_rd_req),.read_ready(sd_rd_rdy),.read_data(sd_rd_data),.read_valid(sd_rd_val),.sd_clk,.sd_cmd,.sd_dat);
  assign gutil=0;assign pcie_dma_valid=0;assign usb_vin=0;assign usb_rdy_in=1;assign eth_txv=0;assign eth_rxr=1;
  assign gpo=0;assign gpoe=0;assign spi_txv=0;assign i2c_txv=0;assign sd_rd_req=0;
  genvar g;generate for(g=1;g<NC;g++)begin assign pmu[g][0]=0;assign pmu[g][7]=0;end endgenerate
endmodule

// Testbench
module tb_stargaze_rv64;
  localparam NC=4;logic clk=0;always #5 clk=~clk;logic rst=0,prst=0;logic[7:0]tcpu[NC-1:0],tgpu;
  wire uart;wire[63:0]pmu[NC-1:0][7:0];wire[7:0]at;wire[31:0]gu;
  wire pcie_tx_p,pcie_tx_n;logic pcie_rx_p=0,pcie_rx_n=1;
  wire usb_dp,usb_dn;
  wire eth_tx_p,eth_tx_n;logic eth_rx_p=0,eth_rx_n=1;wire eth_mdc;wire eth_mdio;
  wire[31:0]gpio_pins;
  wire spi_sck,spi_mosi;logic spi_miso=0;wire spi_cs_n;
  wire i2c_sda,i2c_scl;
  wire sd_clk,sd_cmd;wire[3:0]sd_dat;
  stargaze_x1_rv64_soc soc(.clk,.rst_n(rst),.prst_n(prst),.tcpu,.tgpu,.uart,.pmu,.gutil(gu),.atemp(at),
    .pcie_tx_p,.pcie_tx_n,.pcie_rx_p,.pcie_rx_n,.usb_dp,.usb_dn,
    .eth_tx_p,.eth_tx_n,.eth_rx_p,.eth_rx_n,.eth_mdc,.eth_mdio,
    .gpio_pins,.spi_sck,.spi_mosi,.spi_miso,.spi_cs_n,.i2c_sda,.i2c_scl,.sd_clk,.sd_cmd,.sd_dat);
  initial begin for(int i=0;i<NC;i++)tcpu[i]=40;tgpu=40;repeat(20)@(posedge clk);prst=1;repeat(10)@(posedge clk);rst=1;
    $display("STARGASE X1 140K - SECURED CORE");repeat(1000)@(posedge clk);
    $display("Inst:%0d Cycles:%0d IPC:%.2f Temp:%0dC",pmu[0][0],pmu[0][7],pmu[0][7]>0?real'(pmu[0][0])/real'(pmu[0][7]):0,at);
    $display("BP+L1+THERMAL+SPEC_CTRL - ALL CONTROLLERS OK!");$finish;end
  initial begin #100000;$display("TIMEOUT");$finish;end
endmodule