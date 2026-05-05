//==============================================================================
// stargaze_x1_140k.h - Stargaze X1 140K SoC Hardware Definitions
// Thermal limits, frequency constants, memory map, interrupt controller
// Target: 3.4GHz base / 3.9GHz boost with 512MB shared iGPU memory
//==============================================================================

#ifndef __STARGAZE_X1_140K_H
#define __STARGAZE_X1_140K_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

//==============================================================================
// Version and Identification
//==============================================================================

#define STARGAZE_X1_SOC_ID              0x58313134304B  // "X1140K"
#define STARGAZE_X1_REVISION            0x0100           // Rev 1.0
#define STARGAZE_X1_STEPPING            0x00             // Stepping 0
#define STARGAZE_X1_MANUFACTURER        "Stargaze Semiconductor"
#define STARGAZE_X1_CODENAME            "Stargaze X1 140K"

//==============================================================================
// Clock and Frequency Constants
//==============================================================================

/* Primary clock frequencies */
#define STARGAZE_BASE_FREQ_KHZ          3400000     // 3400 MHz base frequency
#define STARGAZE_BOOST_FREQ_KHZ         3900000     // 3900 MHz boost frequency
#define STARGAZE_BASE_FREQ_MHZ          3400        // In MHz (convenience)
#define STARGAZE_BOOST_FREQ_MHZ         3900        // In MHz (convenience)
#define STARGAZE_BASE_FREQ_HZ           3400000000ULL // In Hz
#define STARGAZE_BOOST_FREQ_HZ          3900000000ULL // In Hz

/* Crystal oscillator */
#define STARGAZE_XTAL_FREQ_MHZ          24          // 24 MHz reference
#define STARGAZE_RTC_FREQ_KHZ           32          // 32.768 kHz RTC

/* PLL multiplier ranges */
#define STARGAZE_PLL_REF_DIV            1           // Reference divider
#define STARGAZE_PLL_FB_DIV_MIN         50          // Min feedback divider (1.2GHz)
#define STARGAZE_PLL_FB_DIV_MAX         163         // Max feedback divider (3.9GHz)
#define STARGAZE_PLL_BASE_MULT          142         // Base 3.4GHz multiplier
#define STARGAZE_PLL_BOOST_MULT         163         // Boost 3.9GHz multiplier
#define STARGAZE_PLL_LOCK_TIMEOUT_US    100         // PLL lock timeout in µs

/* Frequency limits per domain */
#define STARGAZE_CPU_FREQ_MIN_KHZ       1200000     // 1.2 GHz minimum
#define STARGAZE_CPU_FREQ_MAX_KHZ       3900000     // 3.9 GHz maximum
#define STARGAZE_GPU_FREQ_MIN_KHZ       600000      // 600 MHz minimum
#define STARGAZE_GPU_FREQ_MAX_KHZ       1500000     // 1.5 GHz maximum
#define STARGAZE_NOC_FREQ_KHZ           2400000     // 2.4 GHz NoC
#define STARGAZE_MEM_FREQ_KHZ           4008000     // 4.008 GHz LPDDR5X
#define STARGAZE_PERIPH_FREQ_KHZ        600000      // 600 MHz peripheral

/* DVFS operating points (frequency in kHz, voltage in mV) */
#define STARGAZE_OPP_COUNT              9

typedef enum {
    STARGAZE_OPP_LOW_POWER = 0,         // 1.2 GHz @ 600mV
    STARGAZE_OPP_POWER_SAVE,            // 1.8 GHz @ 650mV
    STARGAZE_OPP_EFFICIENCY,            // 2.4 GHz @ 700mV
    STARGAZE_OPP_BALANCED,              // 2.8 GHz @ 750mV
    STARGAZE_OPP_PERFORMANCE,           // 3.2 GHz @ 820mV
    STARGAZE_OPP_BASE,                  // 3.4 GHz @ 850mV (Default)
    STARGAZE_OPP_BOOST1,                // 3.6 GHz @ 900mV
    STARGAZE_OPP_BOOST2,                // 3.8 GHz @ 930mV
    STARGAZE_OPP_BOOST3                 // 3.9 GHz @ 950mV (Max Boost)
} stargaze_opp_index_t;

typedef struct {
    uint32_t freq_khz;
    uint32_t voltage_mv;
    uint32_t pll_mult;
    uint32_t power_mw;
    uint32_t transition_latency_ns;
    bool     boost_allowed;
    bool     sustainable;
    const char *name;
} stargaze_opp_entry_t;

static const stargaze_opp_entry_t stargaze_opp_table[STARGAZE_OPP_COUNT] = {
    [STARGAZE_OPP_LOW_POWER]   = { 1200000, 600, 50,  450,  5000, false, true,  "Low Power 1.2G" },
    [STARGAZE_OPP_POWER_SAVE]  = { 1800000, 650, 75,  680,  4000, false, true,  "Power Save 1.8G" },
    [STARGAZE_OPP_EFFICIENCY]  = { 2400000, 700, 100, 950,  3000, false, true,  "Efficiency 2.4G" },
    [STARGAZE_OPP_BALANCED]    = { 2800000, 750, 117, 1250, 2500, false, true,  "Balanced 2.8G" },
    [STARGAZE_OPP_PERFORMANCE] = { 3200000, 820, 133, 1600, 2000, true,  true,  "Performance 3.2G" },
    [STARGAZE_OPP_BASE]        = { 3400000, 850, 142, 1850, 1500, false, true,  "Base 3.4GHz" },
    [STARGAZE_OPP_BOOST1]      = { 3600000, 900, 150, 2200, 1300, true,  true,  "Boost1 3.6GHz" },
    [STARGAZE_OPP_BOOST2]      = { 3800000, 930, 158, 2600, 1100, true,  true,  "Boost2 3.8GHz" },
    [STARGAZE_OPP_BOOST3]      = { 3900000, 950, 163, 2900, 1000, true,  true,  "Boost3 3.9GHz" }
};

//==============================================================================
// Thermal Management Constants
//==============================================================================

/* Temperature thresholds (in Celsius) */
#define STARGAZE_TEMP_CRITICAL          105         // Critical shutdown
#define STARGAZE_TEMP_THROTTLE          85          // Aggressive throttling
#define STARGAZE_TEMP_THROTTLE_LIGHT    75          // Light throttling
#define STARGAZE_TEMP_WARNING           65          // Warning, prevent boost
#define STARGAZE_TEMP_NORMAL            45          // Normal operating temperature
#define STARGAZE_TEMP_IDLE              35          // Idle temperature target

/* Thermal throttling levels */
typedef enum {
    STARGAZE_THROTTLE_NONE = 0,         // No throttling (below 65°C)
    STARGAZE_THROTTLE_L1,               // 10% frequency reduction (65-75°C)
    STARGAZE_THROTTLE_L2,               // 25% frequency reduction (75-85°C)
    STARGAZE_THROTTLE_L3,               // 50% frequency reduction (85-95°C)
    STARGAZE_THROTTLE_EMERGENCY,        // Emergency throttle (95-105°C)
    STARGAZE_THROTTLE_SHUTDOWN          // System shutdown (>105°C)
} stargaze_throttle_level_t;

/* Thermal sensor configuration */
#define STARGAZE_THERMAL_SENSORS        8           // 8 on-die sensors
#define STARGAZE_THERMAL_HYSTERESIS     5           // 5°C hysteresis
#define STARGAZE_THERMAL_POLL_MS        100         // 100ms polling interval
#define STARGAZE_THERMAL_AVERAGE_N      8           // 8-sample rolling average

/* Thermal zone configurations */
typedef enum {
    STARGAZE_THERMAL_ZONE_CPU_CLUSTER = 0,
    STARGAZE_THERMAL_ZONE_GPU_CORE,
    STARGAZE_THERMAL_ZONE_MEMORY_CTRL,
    STARGAZE_THERMAL_ZONE_NOC,
    STARGAZE_THERMAL_ZONE_L3_CACHE,
    STARGAZE_THERMAL_ZONE_PERIPHERAL,
    STARGAZE_THERMAL_ZONE_SOC_AMBIENT,
    STARGAZE_THERMAL_ZONE_PMU
} stargaze_thermal_zone_t;

typedef struct {
    stargaze_thermal_zone_t zone;
    uint32_t                critical_temp;
    uint32_t                throttle_temp;
    uint32_t                warning_temp;
    uint32_t                poll_ms;
    const char             *name;
} stargaze_thermal_config_t;

static const stargaze_thermal_config_t stargaze_thermal_config[STARGAZE_THERMAL_SENSORS] = {
    { STARGAZE_THERMAL_ZONE_CPU_CLUSTER,  105, 85, 65, 50,  "CPU Cluster" },
    { STARGAZE_THERMAL_ZONE_GPU_CORE,     100, 82, 62, 50,  "GPU Core" },
    { STARGAZE_THERMAL_ZONE_MEMORY_CTRL,   95, 80, 60, 100, "Memory Controller" },
    { STARGAZE_THERMAL_ZONE_NOC,           95, 78, 58, 100, "Network-on-Chip" },
    { STARGAZE_THERMAL_ZONE_L3_CACHE,      90, 75, 55, 100, "L3 Cache" },
    { STARGAZE_THERMAL_ZONE_PERIPHERAL,    85, 70, 50, 200, "Peripheral" },
    { STARGAZE_THERMAL_ZONE_SOC_AMBIENT,   80, 65, 45, 500, "SoC Ambient" },
    { STARGAZE_THERMAL_ZONE_PMU,           90, 75, 55, 100, "PMU" }
};

/* Cooling device states */
#define STARGAZE_COOLING_CPU_MAX_STATE    4
#define STARGAZE_COOLING_GPU_MAX_STATE    3

//==============================================================================
// Power Management Constants
//==============================================================================

/* Voltage regulator settings */
#define STARGAZE_VREG_MIN_MV            600         // 600mV minimum
#define STARGAZE_VREG_MAX_MV            1050        // 1050mV maximum
#define STARGAZE_VREG_STEP_MV           5           // 5mV steps
#define STARGAZE_VREG_BASE_MV           850         // 850mV base
#define STARGAZE_VREG_BOOST_MV          950         // 950mV boost
#define STARGAZE_VREG_RAMP_US           20          // 20µs voltage ramp time

/* Power states */
typedef enum {
    STARGAZE_PSTATE_C0 = 0,             // Active execution
    STARGAZE_PSTATE_C1,                 // Clock gated idle
    STARGAZE_PSTATE_C2,                 // Power gated idle
    STARGAZE_PSTATE_C3,                 // Sleep with cache retention
    STARGAZE_PSTATE_C4,                 // Deep sleep
    STARGAZE_PSTATE_C5,                 // Shutdown
    STARGAZE_PSTATE_C6                  // Power off
} stargaze_power_state_t;

/* Power budgets (in mW) */
#define STARGAZE_POWER_BUDGET_CPU       2500        // 2.5W per CPU core
#define STARGAZE_POWER_BUDGET_GPU       8000        // 8W GPU
#define STARGAZE_POWER_BUDGET_MEM       3000        // 3W memory subsystem
#define STARGAZE_POWER_BUDGET_NOC       1000        // 1W NoC
#define STARGAZE_POWER_BUDGET_SOC       15000       // 15W total SoC

//==============================================================================
// Boost Mode Configuration
//==============================================================================

/* Boost triggers */
#define STARGAZE_BOOST_LOAD_THRESHOLD   85          // 85% utilization to enter boost
#define STARGAZE_BOOST_EXIT_THRESHOLD   65          // 65% utilization to exit boost
#define STARGAZE_BOOST_CONSECUTIVE      3           // 3 consecutive high-load samples
#define STARGAZE_BOOST_MIN_HOLD_MS      50          // Minimum 50ms in boost
#define STARGAZE_BOOST_COOLDOWN_MS      20          // 20ms cooldown between boosts

/* Boost configuration per core type */
typedef struct {
    uint32_t enter_threshold;           // Load percentage to enter boost
    uint32_t exit_threshold;            // Load percentage to exit boost
    uint32_t min_hold_ms;               // Minimum time in boost state
    uint32_t cooldown_ms;               // Cooldown time between boosts
    uint32_t max_boost_temp;            // Maximum temperature for boost
    bool     pstate_aware;              // Consider P-states in boost decision
} stargaze_boost_config_t;

//==============================================================================
// Cache Architecture Constants
//==============================================================================

/* L1 Cache */
#define STARGAZE_L1I_SIZE_KB           128         // 128KB L1 Instruction
#define STARGAZE_L1D_SIZE_KB           128         // 128KB L1 Data
#define STARGAZE_L1I_LINE_BYTES        64          // 64-byte cache lines
#define STARGAZE_L1D_LINE_BYTES        64
#define STARGAZE_L1I_ASSOCIATIVITY     4           // 4-way set associative
#define STARGAZE_L1D_ASSOCIATIVITY     8
#define STARGAZE_L1_LATENCY_CYCLES     4           // 4 cycle L1 access

/* L2 Cache (private per-core) */
#define STARGAZE_L2_SIZE_KB            1024        // 1MB private L2
#define STARGAZE_L2_LINE_BYTES         64
#define STARGAZE_L2_ASSOCIATIVITY      16          // 16-way set associative
#define STARGAZE_L2_LATENCY_CYCLES     12          // 12 cycle L2 access
#define STARGAZE_L2_NUM_BANKS          8           // 8 banks for parallelism

/* L3 Cache (shared system cache) */
#define STARGAZE_L3_SIZE_KB            4096        // 4MB shared L3
#define STARGAZE_L3_LINE_BYTES         64
#define STARGAZE_L3_ASSOCIATIVITY      16
#define STARGAZE_L3_LATENCY_CYCLES     28          // 28 cycle L3 access
#define STARGAZE_L3_NUM_BANKS          16
#define STARGAZE_L3_NUM_PORTS          5           // 4 CPUs + GPU

/* iGPU L2 Cache */
#define STARGAZE_GPU_L2_SIZE_KB        2048        // 2MB GPU L2
#define STARGAZE_GPU_L2_LINE_BYTES     64
#define STARGAZE_GPU_L2_ASSOCIATIVITY  16
#define STARGAZE_GPU_L2_LATENCY_CYCLES 25

//==============================================================================
// Memory Map - Stargaze X1 140K Address Space (40-bit)
//==============================================================================

/* Physical address width */
#define STARGAZE_PHYS_ADDR_WIDTH       40
#define STARGAZE_PHYS_ADDR_MASK        ((1ULL << STARGAZE_PHYS_ADDR_WIDTH) - 1)

/* Memory map base addresses */
#define STARGAZE_DRAM_BASE             0x0000000000ULL  // 512MB LPDDR5X
#define STARGAZE_DRAM_SIZE             0x20000000ULL    // 512MB

#define STARGAZE_IGPU_FB_BASE          0x8000000000ULL  // iGPU Shared Memory
#define STARGAZE_IGPU_FB_SIZE          0x20000000ULL    // 512MB
#define STARGAZE_IGPU_FB_END           (STARGAZE_IGPU_FB_BASE + STARGAZE_IGPU_FB_SIZE - 1)

#define STARGAZE_CPU_MMIO_BASE         0x4000000000ULL  // CPU MMIO region
#define STARGAZE_GPU_MMIO_BASE         0x5000000000ULL  // GPU MMIO region
#define STARGAZE_PERIPH_BASE           0x6000000000ULL  // Peripheral base

/* Detailed memory map */
typedef enum {
    /* DRAM Region (0x00_0000_0000 - 0x1F_FFFF_FFFF) */
    STARGAZE_REGION_DRAM_START        = 0x0000000000ULL,
    STARGAZE_REGION_DRAM_END          = 0x1FFFFFFFFFULL,
    
    /* Boot ROM (0x20_0000_0000 - 0x20_00FF_FFFF) */
    STARGAZE_REGION_BOOT_ROM          = 0x2000000000ULL,
    STARGAZE_REGION_BOOT_ROM_SIZE     = 0x01000000ULL,
    
    /* CPU Private Regions */
    STARGAZE_BASE_CPU0_L2             = 0x2100000000ULL,
    STARGAZE_BASE_CPU1_L2             = 0x2101000000ULL,
    STARGAZE_BASE_CPU2_L2             = 0x2102000000ULL,
    STARGAZE_BASE_CPU3_L2             = 0x2103000000ULL,
    
    /* iGPU Framebuffer (0x80_0000_0000 - 0x9F_FFFF_FFFF) */
    STARGAZE_BASE_IGPU_FB             = STARGAZE_IGPU_FB_BASE,
    STARGAZE_BASE_IGPU_FB_END         = STARGAZE_IGPU_FB_END,
    
    /* GPU Registers */
    STARGAZE_BASE_GPU_CTRL            = 0x5000000000ULL,
    STARGAZE_BASE_GPU_CTRL_SIZE       = 0x00010000ULL,
    STARGAZE_BASE_GPU_MMU             = 0x5001000000ULL,
    STARGAZE_BASE_GPU_DISPLAY         = 0x5002000000ULL,
    
    /* System Control */
    STARGAZE_BASE_CMU                 = 0x6000000000ULL,  // Clock Management
    STARGAZE_BASE_PMU                 = 0x6001000000ULL,  // Power Management
    STARGAZE_BASE_RST                 = 0x6002000000ULL,  // Reset Controller
    STARGAZE_BASE_SYS_CTRL            = 0x6003000000ULL,  // System Control
    
    /* Interrupt Controller */
    STARGAZE_BASE_PLIC                = 0x6004000000ULL,  // Platform-Level IRQ
    STARGAZE_BASE_CLINT               = 0x6005000000ULL,  // Core-Local Interrupts
    STARGAZE_BASE_IPI                 = 0x6006000000ULL,  // Inter-Processor IRQs
    
    /* L3 Cache Controller */
    STARGAZE_BASE_L3_CTRL             = 0x6010000000ULL,
    
    /* DDR Memory Controller */
    STARGAZE_BASE_DDR_CTRL            = 0x6020000000ULL,
    
    /* Network-on-Chip */
    STARGAZE_BASE_NOC                 = 0x6030000000ULL,
    
    /* Peripheral Bus */
    STARGAZE_BASE_UART0               = 0x6100000000ULL,
    STARGAZE_BASE_UART1               = 0x6100001000ULL,
    STARGAZE_BASE_SPI0                = 0x6101000000ULL,
    STARGAZE_BASE_I2C0                = 0x6102000000ULL,
    STARGAZE_BASE_GPIO                = 0x6103000000ULL,
    STARGAZE_BASE_TIMER               = 0x6104000000ULL,
    STARGAZE_BASE_WDT                 = 0x6105000000ULL,
    STARGAZE_BASE_DMA                 = 0x6106000000ULL,
    
    /* PCIe Root Complex */
    STARGAZE_BASE_PCIE                = 0x6200000000ULL,
    STARGAZE_BASE_PCIE_CONFIG         = 0x7000000000ULL,
    STARGAZE_BASE_PCIE_MEM            = 0x8000000000ULL,
    
    /* Debug and Trace */
    STARGAZE_BASE_DEBUG               = 0x6F00000000ULL,
    STARGAZE_BASE_JTAG                = 0x6F01000000ULL,
    STARGAZE_BASE_ETM                 = 0x6F02000000ULL
} stargaze_memory_region_t;

//==============================================================================
// Shared iGPU Memory Configuration
//==============================================================================

/* iGPU memory aperture */
#define STARGAZE_IGPU_MEM_BASE        STARGAZE_IGPU_FB_BASE
#define STARGAZE_IGPU_MEM_SIZE        STARGAZE_IGPU_FB_SIZE
#define STARGAZE_IGPU_MEM_ALIGN       4096        // 4KB alignment

/* iGPU memory regions */
typedef enum {
    STARGAZE_IGPU_MEM_FRAMEBUFFER = 0,    // Primary framebuffer
    STARGAZE_IGPU_MEM_DEPTH_BUFFER,       // Depth/stencil buffer
    STARGAZE_IGPU_MEM_TEXTURES,           // Texture storage
    STARGAZE_IGPU_MEM_SHADERS,            // Shader code
    STARGAZE_IGPU_MEM_COMMAND_BUFFER,     // GPU command buffers
    STARGAZE_IGPU_MEM_VERTEX_BUFFER,      // Vertex data
    STARGAZE_IGPU_MEM_INDEX_BUFFER,       // Index data
    STARGAZE_IGPU_MEM_CONSTANT_BUFFER,    // Uniform/constant buffers
    STARGAZE_IGPU_MEM_COMPUTE,            // Compute shader buffers
    STARGAZE_IGPU_MEM_DISPLAY,            // Display scanout
    STARGAZE_IGPU_MEM_RESERVED            // Reserved for future use
} stargaze_igpu_mem_region_t;

/* Default iGPU memory layout */
typedef struct {
    uint64_t  framebuffer_base;
    uint32_t  framebuffer_size;
    uint64_t  depth_buffer_base;
    uint32_t  depth_buffer_size;
    uint64_t  texture_heap_base;
    uint32_t  texture_heap_size;
    uint64_t  shader_heap_base;
    uint32_t  shader_heap_size;
    uint64_t  command_buffer_base;
    uint32_t  command_buffer_size;
    uint64_t  vertex_buffer_base;
    uint32_t  vertex_buffer_size;
    uint64_t  display_scanout_base;
    uint32_t  display_scanout_size;
} stargaze_igpu_mem_layout_t;

/* Default layout dividing 512MB */
#define STARGAZE_IGPU_DEFAULT_LAYOUT {                              \
    .framebuffer_base    = STARGAZE_IGPU_MEM_BASE,                  \
    .framebuffer_size    = 0x08000000,   /* 128MB */                \
    .depth_buffer_base   = STARGAZE_IGPU_MEM_BASE + 0x08000000,    \
    .depth_buffer_size   = 0x04000000,   /* 64MB */                 \
    .texture_heap_base   = STARGAZE_IGPU_MEM_BASE + 0x0C000000,    \
    .texture_heap_size   = 0x0C000000,   /* 192MB */                \
    .shader_heap_base    = STARGAZE_IGPU_MEM_BASE + 0x18000000,    \
    .shader_heap_size    = 0x02000000,   /* 32MB */                 \
    .command_buffer_base = STARGAZE_IGPU_MEM_BASE + 0x1A000000,    \
    .command_buffer_size = 0x02000000,   /* 32MB */                 \
    .vertex_buffer_base  = STARGAZE_IGPU_MEM_BASE + 0x1C000000,    \
    .vertex_buffer_size  = 0x02000000,   /* 32MB */                 \
    .display_scanout_base = STARGAZE_IGPU_MEM_BASE + 0x1E000000,   \
    .display_scanout_size = 0x02000000,  /* 32MB */                 \
}

//==============================================================================
// Interrupt Controller Configuration
//==============================================================================

/* IRQ counts and configuration */
#define STARGAZE_MAX_IRQ_SOURCES       256
#define STARGAZE_MAX_IRQ_TARGETS       4           // 4 CPU cores
#define STARGAZE_IRQ_PRIORITY_BITS     8           // 256 priority levels
#define STARGAZE_IRQ_MAX_PRIORITY      255
#define STARGAZE_IRQ_DEFAULT_PRIORITY  128

/* PLIC register offsets */
#define PLIC_PRIORITY_BASE             0x00000000
#define PLIC_PRIORITY(n)              (PLIC_PRIORITY_BASE + ((n) * 4))
#define PLIC_PENDING_BASE             0x00001000
#define PLIC_ENABLE_BASE              0x00002000
#define PLIC_ENABLE(hart)            (PLIC_ENABLE_BASE + ((hart) * 0x100))
#define PLIC_THRESHOLD(hart)         (0x00200000 + ((hart) * 0x1000))
#define PLIC_CLAIM(hart)             (0x00200004 + ((hart) * 0x1000))

/* CLINT register offsets */
#define CLINT_MSIP(hart)             (0x0000 + ((hart) * 4))
#define CLINT_MTIMECMP(hart)         (0x4000 + ((hart) * 8))
#define CLINT_MTIME                   0xBFF8

/* Interrupt IDs - Standard RISC-V with Stargaze extensions */
typedef enum {
    /* Software interrupts */
    STARGAZE_IRQ_SOFTWARE_USER       = 0,
    STARGAZE_IRQ_SOFTWARE_SUPERVISOR = 1,
    STARGAZE_IRQ_SOFTWARE_HYPERVISOR = 2,
    STARGAZE_IRQ_SOFTWARE_MACHINE    = 3,
    
    /* Timer interrupts */
    STARGAZE_IRQ_TIMER_USER          = 4,
    STARGAZE_IRQ_TIMER_SUPERVISOR    = 5,
    STARGAZE_IRQ_TIMER_HYPERVISOR    = 6,
    STARGAZE_IRQ_TIMER_MACHINE       = 7,
    
    /* External interrupts start at ID 16 */
    STARGAZE_IRQ_EXTERNAL_BASE       = 16,
    
    /* CPU Core interrupts */
    STARGAZE_IRQ_CPU0_PMU            = 16,
    STARGAZE_IRQ_CPU1_PMU            = 17,
    STARGAZE_IRQ_CPU2_PMU            = 18,
    STARGAZE_IRQ_CPU3_PMU            = 19,
    STARGAZE_IRQ_CPU_DEBUG           = 20,
    STARGAZE_IRQ_CPU_THERMAL         = 21,
    
    /* GPU interrupts */
    STARGAZE_IRQ_GPU_COMPLETE        = 32,
    STARGAZE_IRQ_GPU_FAULT           = 33,
    STARGAZE_IRQ_GPU_HANG            = 34,
    STARGAZE_IRQ_GPU_VSYNC           = 35,
    STARGAZE_IRQ_GPU_FLIP            = 36,
    STARGAZE_IRQ_GPU_POWER           = 37,
    
    /* Memory subsystem interrupts */
    STARGAZE_IRQ_MEM_ECC_ERROR       = 48,
    STARGAZE_IRQ_MEM_ECC_CORRECTED   = 49,
    STARGAZE_IRQ_MEM_BANDWIDTH       = 50,
    STARGAZE_IRQ_MEM_THERMAL         = 51,
    STARGAZE_IRQ_DDR_TRAINING_DONE   = 52,
    
    /* L3 Cache interrupts */
    STARGAZE_IRQ_L3_PARITY_ERROR     = 64,
    STARGAZE_IRQ_L3_FILL_COMPLETE    = 65,
    STARGAZE_IRQ_L3_EVICT_COMPLETE   = 66,
    
    /* DMA interrupts */
    STARGAZE_IRQ_DMA0_COMPLETE       = 80,
    STARGAZE_IRQ_DMA0_ERROR          = 81,
    STARGAZE_IRQ_DMA1_COMPLETE       = 82,
    STARGAZE_IRQ_DMA1_ERROR          = 83,
    
    /* Peripheral interrupts */
    STARGAZE_IRQ_UART0               = 96,
    STARGAZE_IRQ_UART1               = 97,
    STARGAZE_IRQ_SPI0                 = 98,
    STARGAZE_IRQ_I2C0                 = 99,
    STARGAZE_IRQ_GPIO                 = 100,
    STARGAZE_IRQ_TIMER0               = 101,
    STARGAZE_IRQ_TIMER1               = 102,
    STARGAZE_IRQ_WDT                  = 103,
    
    /* PCIe interrupts */
    STARGAZE_IRQ_PCIE_MSI_BASE       = 128,
    STARGAZE_IRQ_PCIE_ERROR          = 136,
    STARGAZE_IRQ_PCIE_HOTPLUG        = 137,
    STARGAZE_IRQ_PCIE_PME            = 138,
    
    /* Display interrupts */
    STARGAZE_IRQ_DP_HOTPLUG          = 144,
    STARGAZE_IRQ_DP_HDCP             = 145,
    STARGAZE_IRQ_DP_AUX              = 146,
    
    /* System interrupts */
    STARGAZE_IRQ_POWER_WARNING       = 160,
    STARGAZE_IRQ_THERMAL_CRITICAL    = 161,
    STARGAZE_IRQ_VREG_FAULT          = 162,
    STARGAZE_IRQ_CLOCK_FAULT         = 163,
    STARGAZE_IRQ_WATCHDOG            = 164,
    STARGAZE_IRQ_SECURITY            = 165,
    
    STARGAZE_IRQ_MAX                 = 256
} stargaze_irq_id_t;

/* Interrupt routing configuration */
typedef struct {
    stargaze_irq_id_t irq_id;
    uint8_t           priority;
    uint8_t           target_hart_mask;  // Bitmask of target CPU cores
    bool              edge_triggered;    // false = level triggered
    bool              wakeup_enabled;    // Can wake from sleep
    const char       *name;
} stargaze_irq_config_t;

/* Default interrupt routing for high-performance multi-core sync */
static const stargaze_irq_config_t stargaze_default_irq_routing[] = {
    /* GPU interrupts - route to CPU0 for low latency */
    { STARGAZE_IRQ_GPU_COMPLETE,        240, 0x01, true,  true,  "GPU Complete" },
    { STARGAZE_IRQ_GPU_FAULT,           250, 0x01, true,  true,  "GPU Fault" },
    { STARGAZE_IRQ_GPU_HANG,            245, 0x01, true,  true,  "GPU Hang" },
    { STARGAZE_IRQ_GPU_VSYNC,           200, 0x01, true,  false, "GPU VSync" },
    { STARGAZE_IRQ_GPU_FLIP,            200, 0x01, true,  false, "GPU Flip" },
    
    /* CPU synchronization - broadcast to all cores */
    { STARGAZE_IRQ_SOFTWARE_SUPERVISOR, 254, 0x0F, false, true,  "IPI" },
    { STARGAZE_IRQ_TIMER_SUPERVISOR,    253, 0x0F, false, true,  "Timer" },
    
    /* Memory - route to closest core */
    { STARGAZE_IRQ_MEM_ECC_ERROR,       220, 0x01, true,  true,  "ECC Error" },
    { STARGAZE_IRQ_MEM_BANDWIDTH,       100, 0x03, true,  false, "Memory BW" },
    
    /* DMA - route to core that initiated */
    { STARGAZE_IRQ_DMA0_COMPLETE,       128, 0x0F, true,  false, "DMA0 Done" },
    { STARGAZE_IRQ_DMA1_COMPLETE,       128, 0x0F, true,  false, "DMA1 Done" },
    
    /* PCIe - high priority for low latency */
    { STARGAZE_IRQ_PCIE_MSI_BASE,       192, 0x02, true,  true,  "PCIe MSI" },
    { STARGAZE_IRQ_PCIE_ERROR,          230, 0x01, true,  true,  "PCIe Error" },
    
    /* Critical system interrupts - all cores */
    { STARGAZE_IRQ_THERMAL_CRITICAL,    255, 0x0F, true,  true,  "Thermal Critical" },
    { STARGAZE_IRQ_POWER_WARNING,       248, 0x0F, true,  true,  "Power Warning" },
    { STARGAZE_IRQ_WATCHDOG,            247, 0x0F, true,  true,  "Watchdog" },
};

//==============================================================================
// Multi-Core Synchronization Primitives
//==============================================================================

/* IPI (Inter-Processor Interrupt) types for synchronization */
typedef enum {
    STARGAZE_IPI_RESCHEDULE     = 0,    // Reschedule on target core
    STARGAZE_IPI_CALL_FUNC      = 1,    // Call function on target core(s)
    STARGAZE_IPI_CPU_STOP       = 2,    // Stop target core
    STARGAZE_IPI_CPU_START      = 3,    // Start target core
    STARGAZE_IPI_TIMER          = 4,    // Timer synchronization
    STARGAZE_IPI_TLB_FLUSH       = 5,    // TLB shootdown
    STARGAZE_IPI_BARRIER        = 6,    // Memory barrier
    STARGAZE_IPI_FENCE_I        = 7,    // Instruction fence
    STARGAZE_IPI_CUSTOM0        = 8,    // Custom IPI 0
    STARGAZE_IPI_CUSTOM1        = 9,    // Custom IPI 1
    STARGAZE_IPI_MAX            = 10
} stargaze_ipi_type_t;

/* IPI message structure (fits in a single 64-bit register) */
typedef union {
    struct {
        uint32_t type      : 4;     // IPI type
        uint32_t target    : 2;     // 0: specific, 1: all, 2: others
        uint32_t reserved  : 2;
        uint32_t source    : 4;     // Source core ID
        uint32_t data      : 20;    // Optional data
    } fields;
    uint32_t raw;
} stargaze_ipi_msg_t;

/* Spinlock for multi-core synchronization */
typedef struct {
    volatile uint32_t lock;
    uint32_t          owner_cpu;
    uint64_t          acquire_count;
    uint64_t          contention_count;
    const char       *name;
} stargaze_spinlock_t;

#define STARGAZE_SPINLOCK_INIT(name) { 0, 0xFFFFFFFF, 0, 0, name }

/* Atomic operations for lock-free synchronization */
static inline uint32_t stargaze_atomic_add(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t result;
    __asm__ __volatile__(
        "amoadd.w %0, %1, (%2)"
        : "=r"(result)
        : "r"(val), "r"(ptr)
        : "memory"
    );
    return result;
}

static inline uint32_t stargaze_atomic_swap(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t result;
    __asm__ __volatile__(
        "amoswap.w %0, %1, (%2)"
        : "=r"(result)
        : "r"(val), "r"(ptr)
        : "memory"
    );
    return result;
}

//==============================================================================
// Hardware Performance Monitoring
//==============================================================================

/* PMU event types */
typedef enum {
    STARGAZE_PMU_CYCLES              = 0,
    STARGAZE_PMU_INSTRUCTIONS        = 1,
    STARGAZE_PMU_BRANCH_MISPRED      = 2,
    STARGAZE_PMU_L1I_MISS            = 3,
    STARGAZE_PMU_L1D_MISS            = 4,
    STARGAZE_PMU_L2_MISS             = 5,
    STARGAZE_PMU_L3_MISS             = 6,
    STARGAZE_PMU_ITLB_MISS           = 7,
    STARGAZE_PMU_DTLB_MISS           = 8,
    STARGAZE_PMU_FP_OPERATIONS       = 9,
    STARGAZE_PMU_VECTOR_OPERATIONS   = 10,
    STARGAZE_PMU_LOAD_OPS            = 11,
    STARGAZE_PMU_STORE_OPS           = 12,
    STARGAZE_PMU_ATOMIC_OPS          = 13,
    STARGAZE_PMU_PIPE_STALLS         = 14,
    STARGAZE_PMU_ICACHE_STALLS       = 15,
    STARGAZE_PMU_DCACHE_STALLS       = 16,
    STARGAZE_PMU_GPU_CYCLES          = 17,
    STARGAZE_PMU_GPU_UTILIZATION     = 18,
    STARGAZE_PMU_MEM_BW_READ         = 19,
    STARGAZE_PMU_MEM_BW_WRITE        = 20,
    STARGAZE_PMU_MAX                 = 32
} stargaze_pmu_event_t;

//==============================================================================
// Boot Configuration
//==============================================================================

/* Boot ROM configuration */
#define STARGAZE_BOOT_ROM_BASE         STARGAZE_REGION_BOOT_ROM
#define STARGAZE_BOOT_ROM_SIZE         0x01000000
#define STARGAZE_BOOT_VECTOR           0x2000000000ULL

/* Boot arguments structure */
typedef struct {
    uint32_t magic;                     // Magic number 0x58314B00
    uint32_t version;                   // Boot protocol version
    uint64_t dtb_address;               // Device tree blob address
    uint64_t kernel_address;            // Kernel load address
    uint64_t initrd_address;            // Initrd address
    uint64_t initrd_size;               // Initrd size
    uint64_t cmdline_address;           // Kernel command line
    uint32_t flags;                     // Boot flags
    uint32_t cpu_freq_khz;              // CPU frequency at boot
    uint32_t gpu_freq_khz;              // GPU frequency at boot
    uint32_t reserved[4];               // Reserved
} stargaze_boot_args_t;

#define STARGAZE_BOOT_MAGIC             0x58314B00  // "X1K\0"

//==============================================================================
// GPU Mailbox Communication
//==============================================================================

/* Mailbox register offsets */
#define STARGAZE_GPU_MBOX_CMD           0x00
#define STARGAZE_GPU_MBOX_DATA          0x04
#define STARGAZE_GPU_MBOX_RESP          0x08
#define STARGAZE_GPU_MBOX_STATUS        0x0C
#define STARGAZE_GPU_MBOX_IRQ_EN        0x10

/* Mailbox commands */
typedef enum {
    GPU_CMD_NOP                = 0x00,
    GPU_CMD_FREQ_CHANGE        = 0x01,
    GPU_CMD_PAUSE_PIPE         = 0x02,
    GPU_CMD_RESUME_PIPE        = 0x03,
    GPU_CMD_QUERY_FREQ         = 0x04,
    GPU_CMD_SET_POWER_STATE    = 0x05,
    GPU_CMD_SUBMIT_COMMAND     = 0x06,
    GPU_CMD_FLUSH_CACHES       = 0x07,
    GPU_CMD_INVALIDATE_TLB     = 0x08,
    GPU_CMD_QUERY_UTILIZATION  = 0x09,
    GPU_CMD_THERMAL_REPORT     = 0x0A,
    GPU_CMD_RESET_ENGINE       = 0x0B
} gpu_mbox_cmd_t;

/* Mailbox response codes */
#define GPU_MBOX_RESP_ACK              0xAA
#define GPU_MBOX_RESP_NACK             0x55
#define GPU_MBOX_RESP_BUSY             0xBB
#define GPU_MBOX_STATUS_BUSY           (1 << 31)

//==============================================================================
// SoC Feature Detection
//==============================================================================

/* Feature register bits */
#define STARGAZE_FEATURE_RVV            (1 << 0)   // Vector extension
#define STARGAZE_FEATURE_HYPERVISOR     (1 << 1)   // Hypervisor support
#define STARGAZE_FEATURE_CRYPTO         (1 << 2)   // Crypto extensions
#define STARGAZE_FEATURE_BITMANIP       (1 << 3)   // Bit manipulation
#define STARGAZE_FEATURE_DOUBLE_FP      (1 << 4)   // Double-precision FP
#define STARGAZE_FEATURE_ATOMIC         (1 << 5)   // Atomic extension
#define STARGAZE_FEATURE_COMPRESSED     (1 << 6)   // Compressed instructions
#define STARGAZE_FEATURE_BOOST          (1 << 7)   // Boost support
#define STARGAZE_FEATURE_PCIE_GEN4      (1 << 8)   // PCIe Gen4
#define STARGAZE_FEATURE_DISPLAYPORT    (1 << 9)   // DisplayPort output
#define STARGAZE_FEATURE_MSAA8x         (1 << 10)  // 8x MSAA support
#define STARGAZE_FEATURE_RAY_TRACING    (1 << 11)  // Ray tracing support

//==============================================================================
// Debug Macros
//==============================================================================

#ifdef STARGAZE_DEBUG
#define stargaze_debug(fmt, ...) \
    printk(KERN_DEBUG "stargaze-x1: " fmt, ##__VA_ARGS__)
#else
#define stargaze_debug(fmt, ...) do {} while (0)
#endif

#define stargaze_info(fmt, ...) \
    printk(KERN_INFO "stargaze-x1: " fmt, ##__VA_ARGS__)
#define stargaze_warn(fmt, ...) \
    printk(KERN_WARNING "stargaze-x1: " fmt, ##__VA_ARGS__)
#define stargaze_err(fmt, ...) \
    printk(KERN_ERR "stargaze-x1: " fmt, ##__VA_ARGS__)
#define stargaze_crit(fmt, ...) \
    printk(KERN_CRIT "stargaze-x1: " fmt, ##__VA_ARGS__)

//==============================================================================
// Compile-Time Assertions
//==============================================================================

/* Verify critical constants at compile time */
_Static_assert(STARGAZE_BASE_FREQ_MHZ == 3400, 
               "Base frequency must be 3400MHz");
_Static_assert(STARGAZE_BOOST_FREQ_MHZ == 3900, 
               "Boost frequency must be 3900MHz");
_Static_assert(STARGAZE_IGPU_FB_SIZE == 0x20000000, 
               "iGPU shared memory must be 512MB");
_Static_assert(STARGAZE_IGPU_MEM_BASE == 0x8000000000ULL, 
               "iGPU memory must start at 0x80_0000_0000");
_Static_assert(STARGAZE_MAX_IRQ_SOURCES == 256, 
               "Must support 256 interrupt sources");
_Static_assert(STARGAZE_OPP_COUNT == 9, 
               "OPP table must have 9 entries");
_Static_assert(STARGAZE_DRAM_SIZE == 0x20000000, 
               "DRAM size must be 512MB");

#ifdef __cplusplus
}
#endif

#endif /* __STARGAZE_X1_140K_H */