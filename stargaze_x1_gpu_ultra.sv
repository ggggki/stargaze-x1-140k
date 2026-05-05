//==============================================================================
// Stargaze X1 Ultra - Stellaris iGPU Architecture
// 16 Compute Units, 256-bit AXI4, Hardware Scheduler
// Target: 3.9GHz operation, 512MB shared memory
//==============================================================================

package stargaze_igpu_pkg;
  // Compute Unit states
  typedef enum logic [2:0] {
    CU_IDLE,
    CU_FETCH_SHADER,
    CU_EXEC_VERTEX,
    CU_EXEC_FRAGMENT,
    CU_EXEC_COMPUTE,
    CU_MEMORY_STALL,
    CU_SYNC_BARRIER,
    CU_CONTEXT_SWITCH
  } cu_state_t;
  
  // Shader types
  typedef enum logic [1:0] {
    SHADER_VERTEX   = 2'b00,
    SHADER_FRAGMENT = 2'b01,
    SHADER_COMPUTE  = 2'b10,
    SHADER_GEOMETRY = 2'b11
  } shader_type_t;
  
  // AXI4 burst types
  typedef enum logic [1:0] {
    AXI_BURST_FIXED = 2'b00,
    AXI_BURST_INCR  = 2'b01,
    AXI_BURST_WRAP  = 2'b10
  } axi_burst_t;
  
  // Texture formats
  typedef enum logic [3:0] {
    TEX_R8G8B8A8_UNORM,
    TEX_R16G16B16A16_FLOAT,
    TEX_R32G32B32A32_FLOAT,
    TEX_D24S8,
    TEX_BC1, TEX_BC3, TEX_BC5, TEX_BC7,
    TEX_R11G11B10_FLOAT,
    TEX_R8_UNORM, TEX_R16_UNORM
  } tex_format_t;
  
  // ROP (Render Output) operations
  typedef enum logic [2:0] {
    ROP_WRITE,
    ROP_BLEND,
    ROP_DEPTH_TEST,
    ROP_STENCIL_TEST,
    ROP_MSAA_RESOLVE
  } rop_op_t;
  
  // Thread group dimensions
  typedef struct packed {
    logic [9:0] x, y, z;
  } thread_group_t;
  
  // Wavefront state
  typedef struct packed {
    logic [5:0]  wave_id;
    logic [31:0] pc;
    logic [255:0] vgpr [15:0];  // 16 vector registers per wave
    logic [31:0] sgpr [7:0];    // 8 scalar registers
    logic [63:0] exec_mask;     // 64-thread execution mask
    logic        active;
  } wavefront_t;
endpackage

import stargaze_igpu_pkg::*;

//==============================================================================
// Stellaris iGPU Top Module
//==============================================================================
module stargaze_stellaris_igpu #(
  parameter int NUM_CU            = 16,
  parameter int NUM_SHADER_ENG    = 4,
  parameter int NUM_RB            = 8,
  parameter int NUM_ROPS          = 32,
  parameter int NUM_TMU           = 64,
  parameter int WAVES_PER_CU      = 16,
  parameter int THREADS_PER_WAVE  = 64,
  parameter int AXI_DATA_WIDTH    = 256,
  parameter int AXI_ADDR_WIDTH    = 40,
  parameter int L0_VECTOR_SIZE    = 16,
  parameter int L1_TEXTURE_SIZE   = 256,
  parameter int L2_GPU_SIZE       = 2048,
  parameter int MAX_DRAW_CALLS    = 4096,
  parameter int MAX_PRIMITIVES    = 65536
)(
  input  logic                     gpu_clk,
  input  logic                     gpu_clk_2x,
  input  logic                     shader_clk,
  input  logic                     rst_n,
  input  logic                     power_on_rst_n,
  
  input  logic                     cpu_cmd_valid,
  output logic                     cpu_cmd_ready,
  input  logic [63:0]              cpu_cmd_data,
  input  logic [3:0]               cpu_cmd_type,
  input  logic [63:0]              cpu_cmd_addr,
  
  output logic                     gpu_irq,
  output logic [7:0]               gpu_irq_vector,
  
  output logic [3:0]               axi_awid,
  output logic [AXI_ADDR_WIDTH-1:0] axi_awaddr,
  output logic [7:0]               axi_awlen,
  output logic [2:0]               axi_awsize,
  output logic [1:0]               axi_awburst,
  output logic                     axi_awvalid,
  input  logic                     axi_awready,
  
  output logic [AXI_DATA_WIDTH-1:0] axi_wdata,
  output logic [AXI_DATA_WIDTH/8-1:0] axi_wstrb,
  output logic                     axi_wlast,
  output logic                     axi_wvalid,
  input  logic                     axi_wready,
  
  input  logic [3:0]               axi_bid,
  input  logic [1:0]               axi_bresp,
  input  logic                     axi_bvalid,
  output logic                     axi_bready,
  
  output logic [3:0]               axi_arid,
  output logic [AXI_ADDR_WIDTH-1:0] axi_araddr,
  output logic [7:0]               axi_arlen,
  output logic [2:0]               axi_arsize,
  output logic [1:0]               axi_arburst,
  output logic                     axi_arvalid,
  input  logic                     axi_arready,
  
  input  logic [3:0]               axi_rid,
  input  logic [AXI_DATA_WIDTH-1:0] axi_rdata,
  input  logic [1:0]               axi_rresp,
  input  logic                     axi_rlast,
  input  logic                     axi_rvalid,
  output logic                     axi_rready,
  
  input  logic [7:0]               gpu_power_budget,
  output logic [7:0]               gpu_power_actual,
  output logic [3:0]               gpu_throttle_level,
  
  output logic [31:0]              gpu_utilization,
  output logic [31:0]              gpu_temperature,
  output logic [63:0]              gpu_frame_counter,
  output logic [63:0]              gpu_primitive_count
);

  //============================================================================
  // MISSING SIGNAL DECLARATIONS - ADDED HERE
  //============================================================================
  logic [31:0] gpu_timestamp;
  logic [NUM_CU-1:0] cu_power_gate;
  logic draw_complete;
  
  // L2 cache request signals
  logic l2_req_valid;
  logic [63:0] l2_req_addr;
  logic l2_req_write;
  logic [AXI_DATA_WIDTH-1:0] l2_req_wdata;
  logic [AXI_DATA_WIDTH/8-1:0] l2_req_wstrb;
  logic [AXI_DATA_WIDTH-1:0] l2_resp_data;
  logic l2_resp_valid;
  
  // L2 cache internal signals
  logic cache_hit;
  logic [AXI_DATA_WIDTH-1:0] cache_line_data;
  logic [39:0] cache_tag_addr;
  logic cache_dirty_line;
  logic [L2_GPU_SIZE*1024-1:0][AXI_DATA_WIDTH-1:0] cache_data;
  logic [32767:0][39:0] cache_tag;
  logic [32767:0] cache_valid;

  //============================================================================
  // Hardware Scheduler
  //============================================================================
  typedef enum logic [3:0] {
    CMD_IDLE,
    CMD_PARSE_DRAW,
    CMD_SETUP_STATE,
    CMD_DISPATCH_SHADERS,
    CMD_WAIT_COMPLETION,
    CMD_HANDLE_SYNC,
    CMD_PROCESS_QUERY,
    CMD_ERROR_RECOVERY
  } cmd_proc_state_t;
  
  cmd_proc_state_t cmd_state, cmd_next_state;
  
  struct packed {
    logic [63:0]  index_buffer_addr;
    logic [63:0]  vertex_buffer_addr;
    logic [63:0]  constant_buffer_addr;
    logic [31:0]  index_count;
    logic [31:0]  vertex_count;
    logic [31:0]  instance_count;
    logic [31:0]  first_index;
    logic [31:0]  first_vertex;
    shader_type_t shader_type;
    logic [15:0]  draw_call_id;
    logic         indexed_draw;
    logic         instanced;
    logic [7:0]   render_target_mask;
  } draw_call_queue [MAX_DRAW_CALLS-1:0];
  
  logic [$clog2(MAX_DRAW_CALLS)-1:0] draw_head_ptr, draw_tail_ptr;
  logic [15:0]                        draw_count;
  
  struct packed {
    logic [7:0]   priority;
    logic [15:0]  draw_id;
    logic [31:0]  submission_time;
    logic [5:0]   target_cu_mask;
    logic         requires_sync;
    logic         preemptable;
  } schedule_entry;
  
  schedule_entry sched_queue [255:0];
  logic [7:0]    sched_rd_ptr, sched_wr_ptr;
  
  // GPU timestamp counter
  always_ff @(posedge gpu_clk or negedge rst_n) begin
    if (!rst_n)
      gpu_timestamp <= '0;
    else
      gpu_timestamp <= gpu_timestamp + 1'b1;
  end
  
  // Scheduler FSM
  always_ff @(posedge gpu_clk or negedge rst_n) begin
    if (!rst_n) begin
      cmd_state <= CMD_IDLE;
      draw_head_ptr <= '0;
      draw_tail_ptr <= '0;
      draw_count <= '0;
      sched_rd_ptr <= '0;
      sched_wr_ptr <= '0;
      cpu_cmd_ready <= 1'b0;
    end else begin
      cmd_state <= cmd_next_state;
      cpu_cmd_ready <= (cmd_state == CMD_IDLE);
      
      if (cpu_cmd_valid && cpu_cmd_ready) begin
        case (cpu_cmd_type)
          4'h0: begin
            draw_call_queue[draw_tail_ptr].draw_call_id <= draw_count;
            draw_tail_ptr <= (draw_tail_ptr + 1'b1) % MAX_DRAW_CALLS;
            draw_count <= draw_count + 16'h1;
            
            sched_queue[sched_wr_ptr].draw_id <= draw_count;
            sched_queue[sched_wr_ptr].priority <= 8'h80;
            sched_queue[sched_wr_ptr].submission_time <= gpu_timestamp;
            sched_wr_ptr <= sched_wr_ptr + 8'h1;
          end
          
          4'h1: cmd_next_state = CMD_SETUP_STATE;
          4'h2: cmd_next_state = CMD_HANDLE_SYNC;
          4'h3: cmd_next_state = CMD_PROCESS_QUERY;
          default: ;
        endcase
      end
      
      if (sched_rd_ptr != sched_wr_ptr) begin
        logic [NUM_CU-1:0] available_cus;
        for (int i = 0; i < NUM_CU; i++)
          available_cus[i] = (cu_state[i] == CU_IDLE);
        
        if (|available_cus) begin
          sched_rd_ptr <= sched_rd_ptr + 8'h1;
          cmd_next_state = CMD_DISPATCH_SHADERS;
        end
      end
    end
  end
  
  //============================================================================
  // Compute Unit Array
  //============================================================================
  cu_state_t         cu_state [NUM_CU-1:0];
  logic [31:0]       cu_pc [NUM_CU-1:0];
  logic [63:0]       cu_active_mask [NUM_CU-1:0];
  logic [255:0]      cu_vgpr [NUM_CU-1:0][15:0];
  logic [31:0]       cu_sgpr [NUM_CU-1:0][7:0];
  logic              cu_stall [NUM_CU-1:0];
  logic [31:0]       cu_instruction [NUM_CU-1:0];
  logic              cu_instruction_valid [NUM_CU-1:0];
  
  logic [NUM_CU-1:0]       l0_icache_req;
  logic [NUM_CU-1:0][63:0] l0_icache_addr;
  logic [NUM_CU-1:0][255:0] l0_icache_rdata;
  logic [NUM_CU-1:0]       l0_icache_hit;
  
  genvar cu_idx;
  generate
    for (cu_idx = 0; cu_idx < NUM_CU; cu_idx++) begin : compute_units
      stargaze_compute_unit #(
        .CU_ID(cu_idx),
        .VGPR_COUNT(16),
        .SGPR_COUNT(8),
        .SIMD_WIDTH(64),
        .LOCAL_MEM_SIZE(65536)
      ) u_compute_unit (
        .clk(gpu_clk),
        .shader_clk(shader_clk),
        .rst_n(rst_n),
        .instruction(cu_instruction[cu_idx]),
        .inst_valid(cu_instruction_valid[cu_idx]),
        .cu_state(cu_state[cu_idx]),
        .stall_out(cu_stall[cu_idx]),
        .active_mask(cu_active_mask[cu_idx]),
        .vgpr_out(cu_vgpr[cu_idx]),
        .sgpr_out(cu_sgpr[cu_idx]),
        .l0_icache_req(l0_icache_req[cu_idx]),
        .l0_icache_addr(l0_icache_addr[cu_idx]),
        .l0_icache_data(l0_icache_rdata[cu_idx]),
        .l0_icache_hit(l0_icache_hit[cu_idx])
      );
    end
  endgenerate
  
  //============================================================================
  // Shader Processor
  //============================================================================
  struct packed {
    logic [63:0] position_x, position_y, position_z, position_w;
    logic [31:0] color_r, color_g, color_b, color_a;
    logic [31:0] texcoord_u, texcoord_v;
    logic [31:0] normal_x, normal_y, normal_z;
    logic [31:0] tangent_x, tangent_y, tangent_z;
    logic [15:0] vertex_id;
    logic        clip_flag;
  } vertex_output [255:0];
  
  typedef struct packed {
    logic [15:0] v0, v1, v2;
    logic [31:0] primitive_id;
    logic [7:0]  render_target;
    logic        front_facing;
    logic [31:0] area;
  } primitive_t;
  
  primitive_t primitive_queue [MAX_PRIMITIVES-1:0];
  logic [$clog2(MAX_PRIMITIVES)-1:0] prim_wr_ptr, prim_rd_ptr;
  
  logic [31:0]  raster_x, raster_y;
  logic [31:0]  raster_z;
  logic [31:0]  raster_w;
  logic [31:0]  raster_barycentric_u, raster_barycentric_v;
  logic [15:0]  raster_tile_x, raster_tile_y;
  logic         fragment_valid;
  
  localparam int TILE_SIZE = 8;
  
  always_ff @(posedge gpu_clk) begin
    if (primitive_queue[prim_rd_ptr].primitive_id != '0) begin
      prim_rd_ptr <= prim_rd_ptr + 1'b1;
      fragment_valid <= 1'b1;
    end
  end
  
  //============================================================================
  // Texture Mapping Units
  //============================================================================
  logic [63:0]       tmu_request_addr [NUM_TMU-1:0];
  tex_format_t       tmu_format [NUM_TMU-1:0];
  logic [31:0]       tmu_u [NUM_TMU-1:0], tmu_v [NUM_TMU-1:0];
  logic [31:0]       tmu_lod [NUM_TMU-1:0];
  logic              tmu_request_valid [NUM_TMU-1:0];
  logic [255:0]      tmu_result_data [NUM_TMU-1:0];
  logic              tmu_result_valid [NUM_TMU-1:0];
  
  logic [NUM_SHADER_ENG-1:0] l1_texture_req;
  logic [NUM_SHADER_ENG-1:0][63:0] l1_texture_addr;
  logic [NUM_SHADER_ENG-1:0][511:0] l1_texture_rdata;
  logic [NUM_SHADER_ENG-1:0] l1_texture_hit;
  
  generate
    for (genvar tmu = 0; tmu < NUM_TMU; tmu++) begin : texture_units
      stargaze_texture_unit #(
        .L1_CACHE_SIZE(4096),
        .MAX_ANISO(16)
      ) u_texture_unit (
        .clk(gpu_clk),
        .rst_n(rst_n),
        .request_addr(tmu_request_addr[tmu]),
        .tex_format(tmu_format[tmu]),
        .texcoord_u(tmu_u[tmu]),
        .texcoord_v(tmu_v[tmu]),
        .lod(tmu_lod[tmu]),
        .request_valid(tmu_request_valid[tmu]),
        .filtered_data(tmu_result_data[tmu]),
        .result_valid(tmu_result_valid[tmu]),
        .l1_cache_req(l1_texture_req[tmu/4]),
        .l1_cache_addr(l1_texture_addr[tmu/4]),
        .l1_cache_data(l1_texture_rdata[tmu/4]),
        .l1_cache_hit(l1_texture_hit[tmu/4])
      );
    end
  endgenerate
  
  //============================================================================
  // Render Output Units
  //============================================================================
  logic [31:0]       rop_color_r [NUM_ROPS-1:0];
  logic [31:0]       rop_color_g [NUM_ROPS-1:0];
  logic [31:0]       rop_color_b [NUM_ROPS-1:0];
  logic [31:0]       rop_color_a [NUM_ROPS-1:0];
  logic [31:0]       rop_depth [NUM_ROPS-1:0];
  logic [7:0]        rop_stencil [NUM_ROPS-1:0];
  logic [31:0]       rop_x [NUM_ROPS-1:0], rop_y [NUM_ROPS-1:0];
  rop_op_t           rop_op [NUM_ROPS-1:0];
  logic              rop_valid [NUM_ROPS-1:0];
  logic [255:0]      rop_write_data [NUM_ROPS-1:0];
  logic [63:0]       rop_write_addr [NUM_ROPS-1:0];
  logic              rop_write_request [NUM_ROPS-1:0];
  
  generate
    for (genvar rop = 0; rop < NUM_ROPS; rop++) begin : render_outputs
      stargaze_rop #(
        .MSAA_SAMPLES(8)
      ) u_rop (
        .clk(gpu_clk),
        .rst_n(rst_n),
        .color_r_in(rop_color_r[rop]),
        .color_g_in(rop_color_g[rop]),
        .color_b_in(rop_color_b[rop]),
        .color_a_in(rop_color_a[rop]),
        .depth_in(rop_depth[rop]),
        .stencil_in(rop_stencil[rop]),
        .pixel_x(rop_x[rop]),
        .pixel_y(rop_y[rop]),
        .operation(rop_op[rop]),
        .valid_in(rop_valid[rop]),
        .write_data(rop_write_data[rop]),
        .write_addr(rop_write_addr[rop]),
        .write_request(rop_write_request[rop])
      );
    end
  endgenerate
  
  //============================================================================
  // L2 Cache
  //============================================================================
  logic [7:0] l2_arbiter_ptr;
  
  always_ff @(posedge gpu_clk or negedge rst_n) begin
    if (!rst_n)
      l2_arbiter_ptr <= '0;
    else if (l2_req_valid && l2_resp_valid)
      l2_arbiter_ptr <= l2_arbiter_ptr + 8'h1;
  end
  
  stargaze_l2_gpu_cache #(
    .CACHE_SIZE(L2_GPU_SIZE * 1024),
    .LINE_SIZE(64),
    .ASSOCIATIVITY(16),
    .AXI_DATA_WIDTH(AXI_DATA_WIDTH),
    .AXI_ADDR_WIDTH(AXI_ADDR_WIDTH)
  ) u_l2_cache (
    .clk(gpu_clk),
    .rst_n(rst_n),
    .req_valid(l2_req_valid),
    .req_addr(l2_req_addr),
    .req_write(l2_req_write),
    .req_wdata(l2_req_wdata),
    .req_wstrb(l2_req_wstrb),
    .resp_data(l2_resp_data),
    .resp_valid(l2_resp_valid),
    .axi_awid(axi_awid),
    .axi_awaddr(axi_awaddr),
    .axi_awlen(axi_awlen),
    .axi_awsize(axi_awsize),
    .axi_awburst(axi_awburst),
    .axi_awvalid(axi_awvalid),
    .axi_awready(axi_awready),
    .axi_wdata(axi_wdata),
    .axi_wstrb(axi_wstrb),
    .axi_wlast(axi_wlast),
    .axi_wvalid(axi_wvalid),
    .axi_wready(axi_wready),
    .axi_bid(axi_bid),
    .axi_bresp(axi_bresp),
    .axi_bvalid(axi_bvalid),
    .axi_bready(axi_bready),
    .axi_arid(axi_arid),
    .axi_araddr(axi_araddr),
    .axi_arlen(axi_arlen),
    .axi_arsize(axi_arsize),
    .axi_arburst(axi_arburst),
    .axi_arvalid(axi_arvalid),
    .axi_arready(axi_arready),
    .axi_rid(axi_rid),
    .axi_rdata(axi_rdata),
    .axi_rresp(axi_rresp),
    .axi_rlast(axi_rlast),
    .axi_rvalid(axi_rvalid),
    .axi_rready(axi_rready)
  );
  
  //============================================================================
  // DCC Engine placeholder
  //============================================================================
  logic [255:0] dcc_input_data, dcc_compressed_data;
  logic [3:0]   dcc_compression_ratio;
  logic         dcc_compress_valid, dcc_decompress_valid;
  
  // Simple pass-through DCC
  always_ff @(posedge gpu_clk) begin
    dcc_compressed_data <= dcc_input_data;
    dcc_compression_ratio <= 4'h1;
    dcc_decompress_valid <= dcc_compress_valid;
  end
  
  //============================================================================
  // Power Management
  //============================================================================
  logic [15:0] gpu_activity_counter;
  logic [7:0]  gpu_temp_sensor;
  logic [3:0]  gpu_throttle;
  
  always_ff @(posedge gpu_clk or negedge rst_n) begin
    if (!rst_n) begin
      gpu_activity_counter <= '0;
      gpu_throttle <= '0;
      gpu_temp_sensor <= 8'd45;
      gpu_frame_counter <= '0;
      gpu_primitive_count <= '0;
      draw_complete <= 1'b0;
      for (int i = 0; i < NUM_CU; i++)
        cu_power_gate[i] <= 1'b0;
    end else begin
      gpu_activity_counter <= 
        (|cu_stall) ? gpu_activity_counter - 16'h10 :
                      gpu_activity_counter + 16'h20;
      
      if (gpu_temp_sensor > 8'd85)
        gpu_throttle <= 4'hF;
      else if (gpu_temp_sensor > 8'd75)
        gpu_throttle <= 4'h7;
      else if (gpu_temp_sensor > 8'd65)
        gpu_throttle <= 4'h3;
      else
        gpu_throttle <= '0;
      
      for (int i = 0; i < NUM_CU; i++) begin
        if (cu_state[i] == CU_IDLE && gpu_throttle > 4'h8)
          cu_power_gate[i] <= 1'b1;
        else
          cu_power_gate[i] <= 1'b0;
      end
      
      gpu_utilization <= (gpu_activity_counter[15:8] * 32'd100) / 256;
      gpu_temperature <= gpu_temp_sensor;
      
      if (draw_complete)
        gpu_frame_counter <= gpu_frame_counter + 64'h1;
      
      gpu_primitive_count <= gpu_primitive_count + 
        {32'b0, primitive_queue[prim_wr_ptr].primitive_id != '0};
    end
  end
  
  assign gpu_power_actual = gpu_activity_counter[15:8];
  assign gpu_throttle_level = gpu_throttle;
  assign gpu_irq = 1'b0;
  assign gpu_irq_vector = '0;

endmodule

//============================================================================
// Compute Unit Module
//============================================================================
module stargaze_compute_unit #(
  parameter int CU_ID          = 0,
  parameter int VGPR_COUNT     = 16,
  parameter int SGPR_COUNT     = 8,
  parameter int SIMD_WIDTH     = 64,
  parameter int LOCAL_MEM_SIZE = 65536
)(
  input  logic                    clk,
  input  logic                    shader_clk,
  input  logic                    rst_n,
  input  logic [31:0]             instruction,
  input  logic                    inst_valid,
  output cu_state_t               cu_state,
  output logic                    stall_out,
  output logic [63:0]             active_mask,
  output logic [255:0]            vgpr_out [VGPR_COUNT-1:0],
  output logic [31:0]             sgpr_out [SGPR_COUNT-1:0],
  output logic                    l0_icache_req,
  output logic [63:0]             l0_icache_addr,
  input  logic [255:0]            l0_icache_data,
  input  logic                    l0_icache_hit
);
  
  import stargaze_igpu_pkg::*;
  
  logic [VGPR_COUNT-1:0][255:0] vgpr;
  logic [SGPR_COUNT-1:0][31:0]  sgpr;
  
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cu_state <= CU_IDLE;
      stall_out <= 1'b0;
      active_mask <= '0;
      l0_icache_req <= 1'b0;
      for (int i = 0; i < VGPR_COUNT; i++)
        vgpr[i] <= '0;
      for (int i = 0; i < SGPR_COUNT; i++)
        sgpr[i] <= '0;
    end else begin
      if (inst_valid) begin
        cu_state <= CU_EXEC_COMPUTE;
        
        case (instruction[31:26])
          6'h00: vgpr[instruction[25:18]] <= vgpr[instruction[17:10]];
          6'h01: vgpr[instruction[25:18]] <= vgpr[instruction[9:2]];
          default: cu_state <= CU_IDLE;
        endcase
      end else begin
        cu_state <= CU_IDLE;
      end
    end
  end
  
  assign vgpr_out = vgpr;
  assign sgpr_out = sgpr;
  assign l0_icache_addr = '0;
  
endmodule

//============================================================================
// Texture Mapping Unit
//============================================================================
module stargaze_texture_unit #(
  parameter int L1_CACHE_SIZE = 4096,
  parameter int MAX_ANISO     = 16
)(
  input  logic                clk,
  input  logic                rst_n,
  input  logic [63:0]         request_addr,
  input  tex_format_t         tex_format,
  input  logic [31:0]         texcoord_u,
  input  logic [31:0]         texcoord_v,
  input  logic [31:0]         lod,
  input  logic                request_valid,
  output logic [255:0]        filtered_data,
  output logic                result_valid,
  output logic                l1_cache_req,
  output logic [63:0]         l1_cache_addr,
  input  logic [511:0]        l1_cache_data,
  input  logic                l1_cache_hit
);
  
  import stargaze_igpu_pkg::*;
  
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      filtered_data <= '0;
      result_valid <= 1'b0;
      l1_cache_req <= 1'b0;
    end else if (request_valid) begin
      l1_cache_req <= 1'b1;
      l1_cache_addr <= request_addr;
      if (l1_cache_hit) begin
        filtered_data <= l1_cache_data[255:0];
        result_valid <= 1'b1;
        l1_cache_req <= 1'b0;
      end
    end
  end
  
endmodule

//============================================================================
// Render Output Pipeline Unit
//============================================================================
module stargaze_rop #(
  parameter int MSAA_SAMPLES = 8
)(
  input  logic                clk,
  input  logic                rst_n,
  input  logic [31:0]         color_r_in, color_g_in, color_b_in, color_a_in,
  input  logic [31:0]         depth_in,
  input  logic [7:0]          stencil_in,
  input  logic [31:0]         pixel_x, pixel_y,
  input  rop_op_t             operation,
  input  logic                valid_in,
  output logic [255:0]        write_data,
  output logic [63:0]         write_addr,
  output logic                write_request
);
  
  import stargaze_igpu_pkg::*;
  
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      write_request <= 1'b0;
      write_data <= '0;
      write_addr <= '0;
    end else if (valid_in) begin
      write_data <= {color_a_in, color_b_in, color_g_in, color_r_in,
                     color_a_in, color_b_in, color_g_in, color_r_in};
      write_addr <= {pixel_x[31:0], pixel_y[31:0]};
      write_request <= 1'b1;
    end else begin
      write_request <= 1'b0;
    end
  end
  
endmodule

//============================================================================
// L2 GPU Cache Controller
//============================================================================
module stargaze_l2_gpu_cache #(
  parameter int CACHE_SIZE     = 2097152,
  parameter int LINE_SIZE      = 64,
  parameter int ASSOCIATIVITY  = 16,
  parameter int AXI_DATA_WIDTH = 256,
  parameter int AXI_ADDR_WIDTH = 40
)(
  input  logic                     clk,
  input  logic                     rst_n,
  
  input  logic                     req_valid,
  input  logic [63:0]              req_addr,
  input  logic                     req_write,
  input  logic [AXI_DATA_WIDTH-1:0] req_wdata,
  input  logic [AXI_DATA_WIDTH/8-1:0] req_wstrb,
  output logic [AXI_DATA_WIDTH-1:0] resp_data,
  output logic                     resp_valid,
  
  output logic [3:0]               axi_awid,
  output logic [AXI_ADDR_WIDTH-1:0] axi_awaddr,
  output logic [7:0]               axi_awlen,
  output logic [2:0]               axi_awsize,
  output logic [1:0]               axi_awburst,
  output logic                     axi_awvalid,
  input  logic                     axi_awready,
  
  output logic [AXI_DATA_WIDTH-1:0] axi_wdata,
  output logic [AXI_DATA_WIDTH/8-1:0] axi_wstrb,
  output logic                     axi_wlast,
  output logic                     axi_wvalid,
  input  logic                     axi_wready,
  
  input  logic [3:0]               axi_bid,
  input  logic [1:0]               axi_bresp,
  input  logic                     axi_bvalid,
  output logic                     axi_bready,
  
  output logic [3:0]               axi_arid,
  output logic [AXI_ADDR_WIDTH-1:0] axi_araddr,
  output logic [7:0]               axi_arlen,
  output logic [2:0]               axi_arsize,
  output logic [1:0]               axi_arburst,
  output logic                     axi_arvalid,
  input  logic                     axi_arready,
  
  input  logic [3:0]               axi_rid,
  input  logic [AXI_DATA_WIDTH-1:0] axi_rdata,
  input  logic [1:0]               axi_rresp,
  input  logic                     axi_rlast,
  input  logic                     axi_rvalid,
  output logic                     axi_rready
);
  
  import stargaze_igpu_pkg::*;
  
  enum logic [1:0] {CACHE_IDLE, CACHE_READ, CACHE_WRITE} cache_state;
  
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cache_state <= CACHE_IDLE;
      axi_awvalid <= 1'b0;
      axi_arvalid <= 1'b0;
      axi_wvalid <= 1'b0;
      axi_bready <= 1'b1;
      axi_rready <= 1'b1;
      resp_valid <= 1'b0;
    end else begin
      case (cache_state)
        CACHE_IDLE: begin
          resp_valid <= 1'b0;
          if (req_valid && !req_write) begin
            axi_arvalid <= 1'b1;
            axi_araddr <= req_addr;
            axi_arlen <= 8'h00;
            axi_arsize <= 3'h5;
            axi_arburst <= AXI_BURST_INCR;
            cache_state <= CACHE_READ;
          end
        end
        
        CACHE_READ: begin
          if (axi_arvalid && axi_arready)
            axi_arvalid <= 1'b0;
          if (axi_rvalid) begin
            resp_data <= axi_rdata;
            resp_valid <= 1'b1;
            cache_state <= CACHE_IDLE;
          end
        end
        
        default: cache_state <= CACHE_IDLE;
      endcase
    end
  end
  
  assign axi_awid = 4'h0;
  assign axi_arid = 4'h0;
  assign axi_wstrb = '1;
  
endmodule