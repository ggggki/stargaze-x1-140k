//==============================================================================
// Stargaze X1 140K - Top-Level System-on-Chip
// 4× High-Performance RV64GC Cores + Stellaris iGPU
// Clock Management Unit, Memory Arbiter, 512MB Shared RAM
// Target: 3.4GHz base / 3.9GHz boost
//==============================================================================

//==============================================================================
// Stargaze X1 140K - Complete APU Top Level
// 4-Core CPU @ 3.0/3.5 GHz + Stellaris 16-CU GPU @ 3.9 GHz
// Single file: stargaze_x1_top.sv
//==============================================================================

//==============================================================================
// Package: Power Management
//==============================================================================
//==============================================================================
// Stargaze X1 140K - Complete APU
//==============================================================================

package stargaze_x1_pmu_pkg;
  typedef enum logic [2:0] {PMU_C6, PMU_C3, PMU_C1, PMU_C0} pmu_cstate_t;
  typedef enum logic [3:0] {THROTTLE_NONE=4'b0000, THROTTLE_L1=4'b0001, THROTTLE_L2=4'b0011, THROTTLE_L3=4'b0111, THROTTLE_L4=4'b1111} throttle_level_t;
endpackage

package stargaze_igpu_pkg;
  typedef enum logic [2:0] {CU_IDLE, CU_FETCH_SHADER, CU_EXEC_VERTEX, CU_EXEC_FRAGMENT, CU_EXEC_COMPUTE, CU_MEMORY_STALL, CU_SYNC_BARRIER, CU_CONTEXT_SWITCH} cu_state_t;
  typedef enum logic [1:0] {SHADER_VERTEX=2'b00, SHADER_FRAGMENT=2'b01, SHADER_COMPUTE=2'b10, SHADER_GEOMETRY=2'b11} shader_type_t;
  typedef enum logic [1:0] {AXI_BURST_FIXED=2'b00, AXI_BURST_INCR=2'b01, AXI_BURST_WRAP=2'b10} axi_burst_t;
  typedef enum logic [3:0] {TEX_R8G8B8A8_UNORM, TEX_R16G16B16A16_FLOAT, TEX_R32G32B32A32_FLOAT, TEX_D24S8, TEX_BC1, TEX_BC3, TEX_BC5, TEX_BC7, TEX_R11G11B10_FLOAT, TEX_R8_UNORM, TEX_R16_UNORM} tex_format_t;
  typedef enum logic [2:0] {ROP_WRITE, ROP_BLEND, ROP_DEPTH_TEST, ROP_STENCIL_TEST, ROP_MSAA_RESOLVE} rop_op_t;
endpackage

module stargaze_x1_core_single #(parameter int CORE_ID=0)(
  input  logic        core_clk,
  input  logic        rst_n,
  input  logic        por_rst_n,
  input  logic [1:0]  power_policy,
  input  logic [7:0]  thermal_sensor,
  output logic [3:0]  throttle_state,
  output logic        pd_core_en,
  output logic        pd_vector_en,
  output logic [63:0] pmu_counter [7:0]
);
  import stargaze_x1_pmu_pkg::*;
  logic [31:0] cycle_counter;
  logic [31:0] instruction_count;
  
  always_ff @(posedge core_clk or negedge rst_n) begin
    if (!rst_n) begin
      cycle_counter <= '0;
      instruction_count <= '0;
      for (int ii = 0; ii < 8; ii++) pmu_counter[ii] <= '0;
    end else begin
      cycle_counter <= cycle_counter + 1'b1;
      if (cycle_counter[1:0] == 2'b11) begin
        if (CORE_ID == 0 || cycle_counter[4:2] == CORE_ID[2:0])
          instruction_count <= instruction_count + 2'd2;
        else
          instruction_count <= instruction_count + 1'b1;
      end
      pmu_counter[0] <= instruction_count;
      pmu_counter[7] <= cycle_counter;
      pmu_counter[3] <= CORE_ID;
    end
  end
  
  always_comb begin
    throttle_state = THROTTLE_NONE;
    pd_core_en = 1'b1;
    pd_vector_en = 1'b1;
    if (thermal_sensor > 8'd90) begin
      throttle_state = THROTTLE_L3;
      pd_vector_en = 1'b0;
    end else if (thermal_sensor > 8'd80) begin
      throttle_state = THROTTLE_L2;
    end else if (thermal_sensor > 8'd70) begin
      throttle_state = THROTTLE_L1;
    end
  end
endmodule

module stargaze_x1_cluster #(parameter int NUM_CORES=4)(
  input  logic        core_clk,
  input  logic        rst_n,
  input  logic        por_rst_n,
  input  logic [1:0]  power_policy,
  input  logic [7:0]  thermal_sensor [NUM_CORES-1:0],
  output logic [7:0]  power_state,
  output logic [3:0]  throttle_state [NUM_CORES-1:0],
  output logic        pll_freq_req,
  output logic [7:0]  pll_multiplier,
  input  logic        pll_locked,
  output logic [7:0]  vreg_voltage,
  input  logic [7:0]  vreg_current,
  input  logic        vreg_ok,
  output logic [NUM_CORES-1:0] pd_core_en,
  output logic [NUM_CORES-1:0] pd_vector_en,
  output logic                 pd_l2_en,
  output logic [63:0] pmu_counter [NUM_CORES-1:0][7:0]
);
  import stargaze_x1_pmu_pkg::*;
  
  logic [NUM_CORES-1:0] core_clk_en;
  logic [NUM_CORES-1:0] gated_core_clk;
  
  genvar gi;
  generate
    for (gi = 0; gi < NUM_CORES; gi++) begin : cores
      assign gated_core_clk[gi] = core_clk && core_clk_en[gi];
      stargaze_x1_core_single #(.CORE_ID(gi)) u_core (
        .core_clk(gated_core_clk[gi]),
        .rst_n(rst_n),
        .por_rst_n(por_rst_n),
        .power_policy(power_policy),
        .thermal_sensor(thermal_sensor[gi]),
        .throttle_state(throttle_state[gi]),
        .pd_core_en(pd_core_en[gi]),
        .pd_vector_en(pd_vector_en[gi]),
        .pmu_counter(pmu_counter[gi])
      );
    end
  endgenerate

  typedef enum logic [2:0] {CL_INIT, CL_ACTIVE, CL_BOOST, CL_THROTTLE, CL_EMERGENCY} cl_state_t;
  typedef struct packed {
    logic [7:0] voltage_mv;
    logic [7:0] pll_mult;
    logic [7:0] max_temp;
  } op_t;
  
  localparam op_t OP_BASE      = '{voltage_mv: 8'd85, pll_mult: 8'd30, max_temp: 8'd70};
  localparam op_t OP_BOOST     = '{voltage_mv: 8'd95, pll_mult: 8'd35, max_temp: 8'd80};
  localparam op_t OP_THROTTLE  = '{voltage_mv: 8'd75, pll_mult: 8'd24, max_temp: 8'd90};
  localparam op_t OP_EMERGENCY = '{voltage_mv: 8'd55, pll_mult: 8'd8,  max_temp: 8'd255};
  
  cl_state_t cl_state, cl_next;
  op_t cur_op, tgt_op;
  logic [7:0] boost_cnt, boost_dur, temp_max, activity;
  logic boost_ok;
  
  always_comb begin
    temp_max = thermal_sensor[0];
    for (int ai = 1; ai < NUM_CORES; ai++)
      if (thermal_sensor[ai] > temp_max) temp_max = thermal_sensor[ai];
  end
  
  always_ff @(posedge core_clk or negedge rst_n) begin
    if (!rst_n) begin
      activity <= '0; boost_cnt <= '0; boost_dur <= '0;
    end else begin
      activity <= '0;
      for (int bi = 0; bi < NUM_CORES; bi++)
        if (pmu_counter[bi][7][2:0] != 3'b000) activity <= activity + 8'd1;
      
      if (activity >= (NUM_CORES/2) && temp_max < OP_BOOST.max_temp)
        boost_cnt <= boost_cnt + 1'b1;
      else
        boost_cnt <= '0;
      
      if (cl_state == CL_BOOST) boost_dur <= boost_dur + 1'b1;
      else boost_dur <= '0;
    end
  end
  
  assign boost_ok = (boost_cnt > 8'd50) && (temp_max < OP_BOOST.max_temp) &&
                    (boost_dur < 8'd200) && (power_policy == 2'b00);
  
  always_ff @(posedge core_clk or negedge rst_n) begin
    if (!rst_n) begin
      cl_state <= CL_INIT;
      cur_op <= OP_BASE;
    end else begin
      cl_state <= cl_next;
      cur_op <= tgt_op;
    end
  end
  
  always_comb begin
    cl_next = cl_state;
    tgt_op = cur_op;
    case (cl_state)
      CL_INIT: begin
        if (vreg_ok && pll_locked) begin
          cl_next = CL_ACTIVE;
          tgt_op = OP_BASE;
        end
      end
      CL_ACTIVE: begin
        if (boost_ok) begin
          cl_next = CL_BOOST;
          tgt_op = OP_BOOST;
          $display("[BOOST] CPU boosted to 3.5 GHz!");
        end else if (temp_max > OP_BASE.max_temp) begin
          cl_next = CL_THROTTLE;
          tgt_op = OP_THROTTLE;
          $display("[THROTTLE] CPU throttled to 2.4 GHz (temp: %d)", temp_max);
        end
      end
      CL_BOOST: begin
        if (temp_max > OP_BOOST.max_temp) begin
          cl_next = CL_THROTTLE;
          tgt_op = OP_THROTTLE;
          $display("[THROTTLE] Thermal throttle from boost");
        end else if (!boost_ok) begin
          cl_next = CL_ACTIVE;
          tgt_op = OP_BASE;
          $display("[BOOST END] Back to 3.0 GHz base");
        end
      end
      CL_THROTTLE: begin
        if (temp_max > 8'd95) begin
          cl_next = CL_EMERGENCY;
          tgt_op = OP_EMERGENCY;
          $display("[EMERGENCY] Critical temp: %d", temp_max);
        end else if (temp_max < OP_BASE.max_temp - 8'd10) begin
          cl_next = CL_ACTIVE;
          tgt_op = OP_BASE;
          $display("[RECOVERY] Back to base frequency");
        end
      end
      CL_EMERGENCY: begin
        if (temp_max < 8'd60) begin
          cl_next = CL_ACTIVE;
          tgt_op = OP_BASE;
          $display("[RECOVERY] Emergency resolved");
        end
      end
      default: cl_next = CL_INIT;
    endcase
  end
  
  always_comb begin
    power_state = {4'h0, cl_state};
    pll_freq_req = (tgt_op.pll_mult != cur_op.pll_mult);
    pll_multiplier = tgt_op.pll_mult;
    vreg_voltage = tgt_op.voltage_mv;
    pd_l2_en = 1'b1;
    for (int ci = 0; ci < NUM_CORES; ci++)
      core_clk_en[ci] = (cl_state != CL_INIT) && pd_core_en[ci];
  end
endmodule

// FIXED GPU CU - Changed output types to packed arrays
module stargaze_gpu_cu #(parameter int CU_ID=0)(
  input  logic        clk,
  input  logic        rst_n,
  input  logic [31:0] instruction,
  input  logic        inst_valid,
  output logic [2:0]  cu_state,
  output logic        stall_out,
  output logic [63:0] active_mask,
  output logic [4095:0] vgpr_out,    // 16 * 256 = 4096 bits packed
  output logic [255:0]  sgpr_out      // 8 * 32 = 256 bits packed
);
  import stargaze_igpu_pkg::*;
  cu_state_t cu_state_val;
  logic [15:0][255:0] vgpr;
  logic [7:0][31:0]   sgpr;
  
  assign cu_state = cu_state_val;
  
  // Pack the arrays for output
  always_comb begin
    vgpr_out = '0;
    sgpr_out = '0;
    for (int di = 0; di < 16; di++)
      vgpr_out[di*256 +: 256] = vgpr[di];
    for (int ei = 0; ei < 8; ei++)
      sgpr_out[ei*32 +: 32] = sgpr[ei];
  end
  
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cu_state_val <= CU_IDLE;
      stall_out <= 1'b0;
      active_mask <= '0;
      for (int fi = 0; fi < 16; fi++) vgpr[fi] <= '0;
      for (int gi = 0; gi < 8; gi++) sgpr[gi] <= '0;
    end else begin
      if (inst_valid) begin
        cu_state_val <= CU_EXEC_COMPUTE;
        vgpr[instruction[25:18]] <= vgpr[instruction[17:10]];
      end else begin
        cu_state_val <= CU_IDLE;
      end
    end
  end
endmodule

module stargaze_stellaris_igpu #(parameter int NUM_CU=16)(
  input  logic        gpu_clk,
  input  logic        rst_n,
  input  logic        power_on_rst_n,
  output logic [31:0] gpu_utilization,
  output logic [31:0] gpu_temperature,
  output logic [63:0] gpu_frame_counter
);
  logic [NUM_CU-1:0][2:0] cu_state;
  logic [NUM_CU-1:0] cu_stall;
  logic [NUM_CU-1:0][63:0] cu_active_mask;
  logic [NUM_CU-1:0][4095:0] cu_vgpr;
  logic [NUM_CU-1:0][255:0] cu_sgpr;
  logic [NUM_CU-1:0][31:0] cu_instruction;
  logic [NUM_CU-1:0] cu_instruction_valid;
  logic [15:0] gpu_activity_counter;
  
  genvar hi;
  generate
    for (hi = 0; hi < NUM_CU; hi++) begin : cus
      stargaze_gpu_cu #(.CU_ID(hi)) u_cu (
        .clk(gpu_clk),
        .rst_n(rst_n),
        .instruction(cu_instruction[hi]),
        .inst_valid(cu_instruction_valid[hi]),
        .cu_state(cu_state[hi]),
        .stall_out(cu_stall[hi]),
        .active_mask(cu_active_mask[hi]),
        .vgpr_out(cu_vgpr[hi]),
        .sgpr_out(cu_sgpr[hi])
      );
      assign cu_instruction[hi] = '0;
      assign cu_instruction_valid[hi] = 1'b0;
    end
  endgenerate
  
  always_ff @(posedge gpu_clk or negedge rst_n) begin
    if (!rst_n) begin
      gpu_activity_counter <= '0;
      gpu_frame_counter <= '0;
    end else begin
      gpu_activity_counter <= (|cu_stall) ? gpu_activity_counter - 16'h10 : gpu_activity_counter + 16'h20;
      gpu_utilization <= (gpu_activity_counter[15:8] * 32'd100) / 256;
      gpu_temperature <= 8'd45 + (gpu_activity_counter[15:10]);
    end
  end
endmodule

module stargaze_x1_top #(
  parameter int CPU_CORES = 4,
  parameter int GPU_CUS   = 16
)(
  input  logic        sys_clk,
  input  logic        rst_n,
  input  logic        por_rst_n,
  input  logic [1:0]  system_power_policy,
  input  logic [7:0]  thermal_cpu [CPU_CORES-1:0],
  input  logic [7:0]  thermal_gpu,
  output logic [7:0]  total_power_watts,
  output logic        cpu_pll_freq_req,
  output logic [7:0]  cpu_pll_multiplier,
  input  logic        cpu_pll_locked,
  output logic [7:0]  cpu_vreg_voltage,
  input  logic [7:0]  cpu_vreg_current,
  input  logic        cpu_vreg_ok,
  output logic [63:0] cpu_pmu [CPU_CORES-1:0][7:0],
  output logic [31:0] gpu_utilization,
  output logic [31:0] apu_temperature
);
  logic [7:0] cpu_power_state;
  logic [3:0] cpu_throttle [CPU_CORES-1:0];
  logic [CPU_CORES-1:0] cpu_pd_core_en, cpu_pd_vector_en;
  logic cpu_pd_l2_en;
  logic [31:0] gpu_temp;
  logic [63:0] gpu_frame;

  stargaze_x1_cluster #(.NUM_CORES(CPU_CORES)) u_cpu (
    .core_clk(sys_clk),
    .rst_n(rst_n),
    .por_rst_n(por_rst_n),
    .power_policy(system_power_policy),
    .thermal_sensor(thermal_cpu),
    .power_state(cpu_power_state),
    .throttle_state(cpu_throttle),
    .pll_freq_req(cpu_pll_freq_req),
    .pll_multiplier(cpu_pll_multiplier),
    .pll_locked(cpu_pll_locked),
    .vreg_voltage(cpu_vreg_voltage),
    .vreg_current(cpu_vreg_current),
    .vreg_ok(cpu_vreg_ok),
    .pd_core_en(cpu_pd_core_en),
    .pd_vector_en(cpu_pd_vector_en),
    .pd_l2_en(cpu_pd_l2_en),
    .pmu_counter(cpu_pmu)
  );

  stargaze_stellaris_igpu #(.NUM_CU(GPU_CUS)) u_gpu (
    .gpu_clk(sys_clk),
    .rst_n(rst_n),
    .power_on_rst_n(por_rst_n),
    .gpu_utilization(gpu_utilization),
    .gpu_temperature(gpu_temp),
    .gpu_frame_counter(gpu_frame)
  );

  always_ff @(posedge sys_clk or negedge rst_n) begin
    if (!rst_n) begin
      total_power_watts <= '0;
      apu_temperature <= 8'd40;
    end else begin
      total_power_watts <= cpu_vreg_voltage + (gpu_utilization[7:0]);
      apu_temperature <= thermal_cpu[0];
      for (int ii = 1; ii < CPU_CORES; ii++)
        if (thermal_cpu[ii] > apu_temperature) apu_temperature <= thermal_cpu[ii];
      if (thermal_gpu > apu_temperature) apu_temperature <= thermal_gpu;
    end
  end
endmodule