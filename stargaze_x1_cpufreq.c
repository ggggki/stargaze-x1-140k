//==============================================================================
// stargaze_cpufreq.c - Stargaze X1 140K CPU Frequency Driver
// Handles 3.4GHz ↔ 3.9GHz transitions with voltage scaling
// Gaming-optimized with thermal awareness
//==============================================================================

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>

#include "stargaze_x1_140k.h"

//==============================================================================
// Driver Constants
//==============================================================================

#define DRIVER_NAME                     "stargaze-cpufreq"
#define DRIVER_VERSION                  "2.0"
#define DRIVER_DESC                     "Stargaze X1 140K CPU Frequency Driver"

/* DVFS transition timing */
#define DVFS_VOLTAGE_STABLE_US          20      // Voltage stabilization time
#define DVFS_PLL_LOCK_TIMEOUT_US        100     // PLL lock timeout
#define DVFS_TRANSITION_TIMEOUT_US      500     // Total transition timeout
#define DVFS_BOOST_HOLD_MIN_US          50000   // Minimum boost hold (50ms)
#define DVFS_BOOST_COOLDOWN_US          20000   // Boost re-entry cooldown (20ms)

/* Load tracking */
#define LOAD_SAMPLE_WINDOW_MS           50      // 50ms load sampling window
#define LOAD_SAMPLES_HISTORY            16      // Keep 16 samples
#define BOOST_ENTRY_LOAD_THRESHOLD      85      // 85% utilization to enter boost
#define BOOST_EXIT_LOAD_THRESHOLD       60      // 60% utilization to exit boost
#define BOOST_CONSECUTIVE_SAMPLES       4       // 4 samples above threshold

/* Gaming-specific optimizations */
#define GAMING_LOAD_BOOST_THRESHOLD     75      // Lower threshold for gaming
#define GAMING_BOOST_HOLD_MS            100000  // 100ms hold for gaming
#define GAMING_FRAME_TIME_TARGET_US     16667   // 60 FPS target (16.667ms)

//==============================================================================
// Hardware Register Offsets
//==============================================================================

#define CMU_CPU_PLL_CTRL                0x0000
#define CMU_CPU_PLL_MULT                0x0004
#define CMU_CPU_PLL_STATUS              0x0008
#define CMU_CPU_CLK_DIV                 0x000C
#define CMU_DVFS_CTRL                   0x0200
#define CMU_DVFS_STATUS                 0x0204
#define CMU_VREG_CTRL                   0x0208
#define CMU_VREG_STATUS                 0x020C
#define CMU_THERMAL_STATUS              0x0300
#define CMU_THERMAL_THRESHOLD           0x0304
#define CMU_PERF_COUNTER0               0x0400
#define CMU_PERF_COUNTER1               0x0408

/* PLL Status Register Bits */
#define PLL_LOCK_BIT                    BIT(0)
#define PLL_FREQ_STABLE_BIT             BIT(1)
#define PLL_BYPASS_BIT                  BIT(2)

/* DVFS Control Register Bits */
#define DVFS_START_TRANSITION           BIT(0)
#define DVFS_VOLTAGE_CHANGE             BIT(1)
#define DVFS_FREQ_CHANGE                BIT(2)
#define DVFS_BOOST_ENABLE               BIT(3)
#define DVFS_THERMAL_THROTTLE           BIT(4)
#define DVFS_GAMING_MODE                BIT(5)
#define DVFS_TRANSITION_DIR             BIT(6)  // 0=down, 1=up

/* DVFS Status Register Bits */
#define DVFS_STATUS_BUSY                BIT(0)
#define DVFS_STATUS_VOLTAGE_OK          BIT(1)
#define DVFS_STATUS_FREQ_OK             BIT(2)
#define DVFS_STATUS_COMPLETE            BIT(3)
#define DVFS_STATUS_ERROR               BIT(4)

//==============================================================================
// OPP Entry Structure
//==============================================================================

struct stargaze_opp {
    u32     freq_khz;
    u32     voltage_mv;
    u32     pll_mult;
    u32     power_mw;
    u32     latency_ns;
    bool    boost_allowed;
    bool    gaming_boost;
    u8      thermal_floor;      // Minimum temperature for this OPP
    u8      thermal_ceiling;    // Maximum temperature for this OPP
    const char *name;
};

/* Complete OPP table matching stargaze_x1_specs.h */
static const struct stargaze_opp stargaze_opp_table[] = {
    /* { freq_khz,    mV,  mult, power, lat_ns, boost, gaming, t_floor, t_ceil, name } */
    {  1200000,      600,   50,    450,  5000, false, false,    0, 105, "1.2GHz Low Power" },
    {  1800000,      650,   75,    680,  4000, false, false,    0, 105, "1.8GHz Power Save" },
    {  2400000,      700,  100,    950,  3000, false, false,    0, 105, "2.4GHz Efficiency" },
    {  2800000,      750,  117,   1250,  2500, false, false,    0, 100, "2.8GHz Balanced" },
    {  3200000,      820,  133,   1600,  2000, true,  true,    30,  90, "3.2GHz Performance" },
    {  3400000,      850,  142,   1850,  1500, false, true,    35,  85, "3.4GHz Base" },
    {  3600000,      900,  150,   2200,  1300, true,  true,    40,  80, "3.6GHz Boost 1" },
    {  3800000,      930,  158,   2600,  1100, true,  true,    45,  75, "3.8GHz Boost 2" },
    {  3900000,      950,  163,   2900,  1000, true,  true,    50,  65, "3.9GHz Max Boost" },
};

#define OPP_COUNT                   ARRAY_SIZE(stargaze_opp_table)
#define OPP_DEFAULT_INDEX           5   // 3.4GHz base
#define OPP_BOOST_START_INDEX       6   // 3.6GHz first boost
#define OPP_MAX_BOOST_INDEX         8   // 3.9GHz max
#define OPP_MIN_INDEX               0

//==============================================================================
// Per-CPU Policy Data
//==============================================================================

struct stargaze_cpu_data {
    struct cpufreq_policy   *policy;
    struct device           *cpu_dev;
    
    /* Hardware resources */
    void __iomem            *cmu_base;
    struct clk              *cpu_clk;
    struct regulator        *vreg_cpu;
    
    /* Current state */
    int                     current_opp_idx;
    int                     target_opp_idx;
    u32                     current_freq_khz;
    u32                     current_voltage_mv;
    
    /* Boost management */
    bool                    boost_active;
    bool                    gaming_mode;
    ktime_t                 boost_entry_time;
    ktime_t                 last_boost_exit_time;
    u32                     consecutive_high_load;
    u32                     consecutive_low_load;
    
    /* Load tracking */
    u32                     load_history[LOAD_SAMPLES_HISTORY];
    u8                      load_history_idx;
    u32                     avg_load;
    u32                     peak_load;
    
    /* Thermal state */
    int                     thermal_zone_id;
    u32                     current_temp;
    bool                    thermal_throttle_active;
    int                     throttle_level;
    
    /* Statistics */
    u64                     transition_count;
    u64                     boost_entries;
    u64                     boost_time_total_us;
    u64                     throttle_events;
    u64                     last_frame_time_us;
    
    /* Synchronization */
    struct mutex            dvfs_lock;
    struct completion       transition_done;
    struct work_struct      boost_eval_work;
    struct delayed_work     thermal_check_work;
    
    /* Frequency table for cpufreq core */
    struct cpufreq_frequency_table *freq_table;
};

//==============================================================================
// Hardware Access Functions
//==============================================================================

static inline u32 cmu_readl(struct stargaze_cpu_data *scd, u32 offset)
{
    return readl(scd->cmu_base + offset);
}

static inline void cmu_writel(struct stargaze_cpu_data *scd, u32 val, u32 offset)
{
    writel(val, scd->cmu_base + offset);
}

/* Read PLL status with timeout */
static int cmu_wait_pll_lock(struct stargaze_cpu_data *scd, u32 timeout_us)
{
    u32 timeout = timeout_us;
    
    while (timeout--) {
        u32 status = cmu_readl(scd, CMU_CPU_PLL_STATUS);
        
        if ((status & PLL_LOCK_BIT) && (status & PLL_FREQ_STABLE_BIT)) {
            return 0;
        }
        udelay(1);
    }
    
    return -ETIMEDOUT;
}

/* Wait for DVFS transition completion */
static int cmu_wait_dvfs_complete(struct stargaze_cpu_data *scd, u32 timeout_us)
{
    u32 timeout = timeout_us;
    
    while (timeout--) {
        u32 status = cmu_readl(scd, CMU_DVFS_STATUS);
        
        if (status & DVFS_STATUS_ERROR) {
            dev_err(scd->cpu_dev, "DVFS transition error detected\n");
            return -EIO;
        }
        
        if (status & DVFS_STATUS_COMPLETE) {
            return 0;
        }
        udelay(1);
    }
    
    dev_err(scd->cpu_dev, "DVFS transition timeout\n");
    return -ETIMEDOUT;
}

//==============================================================================
// Voltage Management
//==============================================================================

/*
 * Scale CPU voltage to target
 * Voltage must be stable before frequency changes
 */
static int stargaze_set_voltage(struct stargaze_cpu_data *scd, 
                                 u32 target_mv)
{
    u32 current_mv = scd->current_voltage_mv;
    int ret;
    
    if (target_mv == current_mv) {
        return 0;  // No change needed
    }
    
    dev_dbg(scd->cpu_dev, "Voltage scaling: %umV → %umV\n", 
            current_mv, target_mv);
    
    if (target_mv > current_mv) {
        /*
         * Scaling UP: Increase voltage first
         * Use stepping for stability if large jump
         */
        u32 step_mv = 5;  // 5mV steps
        u32 steps = (target_mv - current_mv) / step_mv;
        
        for (u32 i = 0; i < steps; i++) {
            u32 intermediate_mv = min(current_mv + (i + 1) * step_mv, target_mv);
            
            ret = regulator_set_voltage(scd->vreg_cpu,
                                        intermediate_mv * 1000,
                                        intermediate_mv * 1000);
            if (ret) {
                dev_err(scd->cpu_dev, 
                        "Failed to set voltage %umV: %d\n",
                        intermediate_mv, ret);
                return ret;
            }
            udelay(DVFS_VOLTAGE_STABLE_US / steps);
        }
    } else {
        /*
         * Scaling DOWN: Decrease voltage
         * Can do in larger steps since undervoltage isn't a risk
         */
        ret = regulator_set_voltage(scd->vreg_cpu,
                                     target_mv * 1000,
                                     target_mv * 1000);
        if (ret) {
            dev_err(scd->cpu_dev, 
                    "Failed to set voltage %umV: %d\n",
                    target_mv, ret);
            return ret;
        }
    }
    
    /* Wait for voltage to stabilize */
    udelay(DVFS_VOLTAGE_STABLE_US);
    
    /* Verify voltage */
    if (!regulator_is_enabled(scd->vreg_cpu)) {
        dev_err(scd->cpu_dev, "Regulator disabled after voltage change!\n");
        return -EIO;
    }
    
    scd->current_voltage_mv = target_mv;
    
    return 0;
}

//==============================================================================
// Frequency Management
//==============================================================================

/*
 * Set CPU PLL frequency by programming multiplier
 */
static int stargaze_set_frequency(struct stargaze_cpu_data *scd,
                                   u32 pll_mult, u32 target_khz)
{
    u32 ctrl;
    int ret;
    
    dev_dbg(scd->cpu_dev, "Frequency scaling: %u MHz → %u MHz (mult: %u)\n",
            scd->current_freq_khz / 1000, target_khz / 1000, pll_mult);
    
    /* Put PLL in bypass during frequency change */
    ctrl = cmu_readl(scd, CMU_CPU_PLL_CTRL);
    ctrl |= PLL_BYPASS_BIT;
    cmu_writel(scd, ctrl, CMU_CPU_PLL_CTRL);
    
    /* Program new multiplier */
    cmu_writel(scd, pll_mult, CMU_CPU_PLL_MULT);
    
    /* Take PLL out of bypass */
    ctrl &= ~PLL_BYPASS_BIT;
    cmu_writel(scd, ctrl, CMU_CPU_PLL_CTRL);
    
    /* Wait for PLL to lock at new frequency */
    ret = cmu_wait_pll_lock(scd, DVFS_PLL_LOCK_TIMEOUT_US);
    if (ret) {
        dev_err(scd->cpu_dev, "PLL lock timeout at multiplier %u\n", pll_mult);
        return ret;
    }
    
    scd->current_freq_khz = target_khz;
    
    return 0;
}

//==============================================================================
// DVFS Transition Core
//==============================================================================

/*
 * Execute a complete DVFS transition between OPPs
 * 
 * For UP transitions (increasing frequency):
 *   1. Increase voltage to target
 *   2. Wait for voltage stability
 *   3. Increase frequency to target
 *   4. Wait for PLL lock
 *
 * For DOWN transitions (decreasing frequency):
 *   1. Decrease frequency to target
 *   2. Wait for PLL lock
 *   3. Decrease voltage to target
 *   4. Wait for voltage stability
 *
 * This sequence prevents both undervoltage (on up) and
 * overvoltage stress (on down).
 */
static int stargaze_dvfs_transition(struct stargaze_cpu_data *scd,
                                     int target_idx)
{
    const struct stargaze_opp *current_opp = &stargaze_opp_table[scd->current_opp_idx];
    const struct stargaze_opp *target_opp = &stargaze_opp_table[target_idx];
    bool scaling_up = (target_opp->freq_khz > current_opp->freq_khz);
    int ret;
    
    dev_info(scd->cpu_dev, 
             "DVFS transition: %s (%u MHz, %u mV) → %s (%u MHz, %u mV) [%s]\n",
             current_opp->name, current_opp->freq_khz / 1000, current_opp->voltage_mv,
             target_opp->name, target_opp->freq_khz / 1000, target_opp->voltage_mv,
             scaling_up ? "UP" : "DOWN");
    
    mutex_lock(&scd->dvfs_lock);
    
    /* Signal DVFS start to hardware */
    cmu_writel(scd, DVFS_START_TRANSITION | 
                     (scaling_up ? DVFS_TRANSITION_DIR : 0),
               CMU_DVFS_CTRL);
    
    if (scaling_up) {
        /* UP: Voltage first, then frequency */
        
        /* Step 1: Raise voltage */
        cmu_writel(scd, DVFS_VOLTAGE_CHANGE, CMU_DVFS_STATUS);
        
        ret = stargaze_set_voltage(scd, target_opp->voltage_mv);
        if (ret) {
            dev_err(scd->cpu_dev, "Voltage raise failed: %d\n", ret);
            goto err_abort;
        }
        
        /* Step 2: Raise frequency */
        cmu_writel(scd, DVFS_FREQ_CHANGE, CMU_DVFS_STATUS);
        
        ret = stargaze_set_frequency(scd, target_opp->pll_mult, 
                                      target_opp->freq_khz);
        if (ret) {
            dev_err(scd->cpu_dev, "Frequency raise failed: %d\n", ret);
            /* Emergency: lower voltage back */
            stargaze_set_voltage(scd, current_opp->voltage_mv);
            goto err_abort;
        }
        
    } else {
        /* DOWN: Frequency first, then voltage */
        
        /* Step 1: Lower frequency */
        cmu_writel(scd, DVFS_FREQ_CHANGE, CMU_DVFS_STATUS);
        
        ret = stargaze_set_frequency(scd, target_opp->pll_mult,
                                      target_opp->freq_khz);
        if (ret) {
            dev_err(scd->cpu_dev, "Frequency lower failed: %d\n", ret);
            goto err_abort;
        }
        
        /* Step 2: Lower voltage (safely, since freq is already down) */
        cmu_writel(scd, DVFS_VOLTAGE_CHANGE, CMU_DVFS_STATUS);
        
        ret = stargaze_set_voltage(scd, target_opp->voltage_mv);
        if (ret) {
            dev_err(scd->cpu_dev, "Voltage lower failed: %d\n", ret);
            /* Not critical - can continue at lower voltage target */
        }
    }
    
    /* Wait for DVFS complete signal from hardware */
    ret = cmu_wait_dvfs_complete(scd, DVFS_TRANSITION_TIMEOUT_US);
    if (ret) {
        dev_err(scd->cpu_dev, "DVFS completion timeout\n");
        goto err_abort;
    }
    
    /* Update state */
    scd->current_opp_idx = target_idx;
    scd->transition_count++;
    
    /* Clear DVFS control */
    cmu_writel(scd, 0, CMU_DVFS_CTRL);
    cmu_writel(scd, 0, CMU_DVFS_STATUS);
    
    mutex_unlock(&scd->dvfs_lock);
    
    dev_info(scd->cpu_dev, "DVFS transition complete: now at %s\n",
             target_opp->name);
    
    return 0;
    
err_abort:
    cmu_writel(scd, DVFS_STATUS_ERROR, CMU_DVFS_STATUS);
    mutex_unlock(&scd->dvfs_lock);
    return ret;
}

//==============================================================================
// Gaming Mode Detection and Optimization
//==============================================================================

/*
 * Detect gaming workload patterns:
 * - Sustained moderate-to-high load
 * - Regular frame intervals (16.7ms for 60 FPS)
 * - Rapid GPU command submission
 */
static bool stargaze_detect_gaming(struct stargaze_cpu_data *scd)
{
    u32 recent_loads[4];
    u32 avg_recent = 0;
    bool regular_intervals = true;
    
    /* Check last 4 load samples */
    for (int i = 0; i < 4; i++) {
        u8 idx = (scd->load_history_idx - 1 - i) % LOAD_SAMPLES_HISTORY;
        recent_loads[i] = scd->load_history[idx];
        avg_recent += recent_loads[i];
    }
    avg_recent /= 4;
    
    /*
     * Gaming detection criteria:
     * 1. Sustained load above 60% (lower than generic boost)
     * 2. Load variation less than 20% (consistent, not bursty)
     * 3. Temperature within safe range
     * 4. Not currently throttled
     */
    if (avg_recent < 60) {
        return false;
    }
    
    /* Check load consistency - gaming is steady, burst is spiky */
    u32 max_load = 0, min_load = 100;
    for (int i = 0; i < 4; i++) {
        max_load = max(max_load, recent_loads[i]);
        min_load = min(min_load, recent_loads[i]);
    }
    
    if ((max_load - min_load) > 25) {
        return false;  // Too much variation for gaming
    }
    
    /* Temperature check */
    if (scd->current_temp > 80) {
        return false;  // Too hot for gaming boost
    }
    
    return true;
}

/*
 * Apply gaming-specific frequency optimizations
 */
static int stargaze_gaming_optimize(struct stargaze_cpu_data *scd)
{
    const struct stargaze_opp *current_opp = 
        &stargaze_opp_table[scd->current_opp_idx];
    int target_idx = scd->current_opp_idx;
    
    if (!scd->gaming_mode) {
        return 0;
    }
    
    /*
     * Gaming strategy:
     * - Prefer 3.6-3.8GHz range for sustained gaming (best perf/watt)
     * - Only use 3.9GHz for loading screens/burst moments
     * - Hold frequency stable to avoid frame time variance
     */
    
    if (scd->avg_load >= 70 && scd->current_opp_idx < OPP_BOOST_START_INDEX) {
        /* Heavy gaming - enter boost range */
        target_idx = OPP_BOOST_START_INDEX;  // 3.6GHz
    } else if (scd->avg_load >= 85 && scd->current_opp_idx < 7) {
        /* Very heavy - 3.8GHz */
        target_idx = 7;
    } else if (scd->avg_load >= 95 && scd->current_opp_idx < OPP_MAX_BOOST_INDEX) {
        /* Maxed out - 3.9GHz */
        target_idx = OPP_MAX_BOOST_INDEX;
    } else if (scd->avg_load < 50 && scd->current_opp_idx > OPP_DEFAULT_INDEX) {
        /* Game paused/menu - drop to base */
        target_idx = OPP_DEFAULT_INDEX;
    }
    
    if (target_idx != scd->current_opp_idx) {
        return stargaze_dvfs_transition(scd, target_idx);
    }
    
    return 0;
}

//==============================================================================
// Thermal Management
//==============================================================================

/*
 * Read current temperature from hardware sensor
 */
static u32 stargaze_read_temperature(struct stargaze_cpu_data *scd)
{
    u32 raw = cmu_readl(scd, CMU_THERMAL_STATUS);
    
    /* Temperature is in millidegrees Celsius */
    return (raw & 0xFFF) / 1000;
}

/*
 * Check thermal conditions and throttle if necessary
 */
static int stargaze_thermal_check(struct stargaze_cpu_data *scd)
{
    u32 temp = stargaze_read_temperature(scd);
    int target_idx = scd->current_opp_idx;
    bool should_throttle = false;
    int throttle_opp_offset = 0;
    
    scd->current_temp = temp;
    
    /*
     * Progressive thermal throttling:
     * - 65°C: Prevent max boost
     * - 75°C: Drop 1 OPP level
     * - 85°C: Drop 2 OPP levels
     * - 95°C: Drop to minimum
     * - 105°C: Emergency shutdown (handled by PMU)
     */
    
    if (temp >= STARGAZE_TEMP_CRITICAL) {
        dev_crit(scd->cpu_dev, 
                 "CRITICAL temperature %u°C! Forcing minimum frequency!\n",
                 temp);
        target_idx = OPP_MIN_INDEX;
        should_throttle = true;
        throttle_opp_offset = 9;
        scd->throttle_events++;
        
    } else if (temp >= STARGAZE_TEMP_THROTTLE) {
        dev_warn(scd->cpu_dev, 
                 "Throttling: %u°C (dropping 2 OPP levels)\n", temp);
        target_idx = max(OPP_MIN_INDEX, scd->current_opp_idx - 2);
        should_throttle = true;
        throttle_opp_offset = 2;
        scd->throttle_events++;
        
        /* Force exit boost */
        if (scd->boost_active) {
            scd->boost_active = false;
            scd->last_boost_exit_time = ktime_get();
        }
        
    } else if (temp >= STARGAZE_TEMP_THROTTLE_LIGHT) {
        dev_info(scd->cpu_dev, 
                 "Light throttle: %u°C (dropping 1 OPP)\n", temp);
        target_idx = max(OPP_MIN_INDEX, scd->current_opp_idx - 1);
        should_throttle = true;
        throttle_opp_offset = 1;
        
    } else if (temp >= STARGAZE_TEMP_WARNING) {
        dev_dbg(scd->cpu_dev, 
                "Warning temp %u°C - limiting boost\n", temp);
        
        /* Prevent entering highest boost levels */
        if (scd->current_opp_idx > OPP_DEFAULT_INDEX + 1) {
            target_idx = OPP_DEFAULT_INDEX + 1;
        }
        
    } else if (temp <= STARGAZE_TEMP_NORMAL) {
        /* Temperature back to normal - allow full performance */
        if (scd->thermal_throttle_active) {
            dev_info(scd->cpu_dev, 
                     "Temperature normalized to %u°C - removing throttle\n",
                     temp);
            scd->thermal_throttle_active = false;
            scd->throttle_level = 0;
        }
    }
    
    /* Update throttle state */
    if (should_throttle) {
        scd->thermal_throttle_active = true;
        scd->throttle_level = throttle_opp_offset;
        
        /* Set hardware thermal throttle flag */
        u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
        ctrl |= DVFS_THERMAL_THROTTLE;
        cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
    } else if (scd->thermal_throttle_active && temp < STARGAZE_TEMP_NORMAL) {
        /* Clear hardware thermal throttle */
        u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
        ctrl &= ~DVFS_THERMAL_THROTTLE;
        cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
        scd->thermal_throttle_active = false;
        scd->throttle_level = 0;
    }
    
    /* Perform transition if target changed */
    if (target_idx != scd->current_opp_idx) {
        return stargaze_dvfs_transition(scd, target_idx);
    }
    
    return 0;
}

//==============================================================================
// Load Tracking
//==============================================================================

/*
 * Sample current CPU load from hardware performance counters
 */
static u32 stargaze_sample_load(struct stargaze_cpu_data *scd)
{
    u32 active_cycles, total_cycles;
    
    /* Read performance counters */
    active_cycles = cmu_readl(scd, CMU_PERF_COUNTER0);
    total_cycles = cmu_readl(scd, CMU_PERF_COUNTER1);
    
    if (total_cycles == 0) {
        return 0;
    }
    
    /* Calculate load percentage */
    u32 load = (active_cycles * 100) / total_cycles;
    
    /* Reset counters for next sample */
    cmu_writel(scd, 0, CMU_PERF_COUNTER0);
    cmu_writel(scd, 0, CMU_PERF_COUNTER1);
    
    return min(load, 100u);
}

/*
 * Update load history and calculate averages
 */
static void stargaze_update_load(struct stargaze_cpu_data *scd)
{
    u32 load = stargaze_sample_load(scd);
    
    /* Store in history buffer */
    scd->load_history[scd->load_history_idx % LOAD_SAMPLES_HISTORY] = load;
    scd->load_history_idx++;
    
    /* Calculate rolling average */
    u32 sum = 0;
    u32 peak = 0;
    u8 valid_samples = min_t(u8, scd->load_history_idx, LOAD_SAMPLES_HISTORY);
    
    for (u8 i = 0; i < valid_samples; i++) {
        sum += scd->load_history[i];
        peak = max(peak, scd->load_history[i]);
    }
    
    scd->avg_load = sum / valid_samples;
    scd->peak_load = peak;
    
    /* Track consecutive high/low load for boost decisions */
    if (load >= BOOST_ENTRY_LOAD_THRESHOLD) {
        scd->consecutive_high_load++;
        scd->consecutive_low_load = 0;
    } else if (load < BOOST_EXIT_LOAD_THRESHOLD) {
        scd->consecutive_low_load++;
        scd->consecutive_high_load = 0;
    } else {
        scd->consecutive_high_load = max(0, (int)scd->consecutive_high_load - 1);
        scd->consecutive_low_load = max(0, (int)scd->consecutive_low_load - 1);
    }
}

//==============================================================================
// Boost Decision Engine
//==============================================================================

/*
 * Evaluate whether to enter or exit boost mode
 */
static int stargaze_evaluate_boost(struct stargaze_cpu_data *scd)
{
    const struct stargaze_opp *current_opp = 
        &stargaze_opp_table[scd->current_opp_idx];
    ktime_t now = ktime_get();
    int target_idx = scd->current_opp_idx;
    bool should_boost = scd->boost_active;
    
    /* Check boost eligibility */
    bool temp_ok = (scd->current_temp <= STARGAZE_TEMP_WARNING);
    bool not_throttled = !scd->thermal_throttle_active;
    bool cooldown_ok = ktime_us_delta(now, scd->last_boost_exit_time) 
                       >= DVFS_BOOST_COOLDOWN_US;
    
    if (scd->boost_active) {
        /* Currently boosted - check exit conditions */
        ktime_t boost_duration = ktime_sub(now, scd->boost_entry_time);
        u64 boost_us = ktime_to_us(boost_duration);
        
        /* Minimum hold time - don't exit too quickly */
        if (boost_us < DVFS_BOOST_HOLD_MIN_US) {
            return 0;  // Maintain boost during minimum hold
        }
        
        /* Exit if load drops significantly */
        if (scd->consecutive_low_load >= BOOST_CONSECUTIVE_SAMPLES &&
            scd->avg_load < BOOST_EXIT_LOAD_THRESHOLD) {
            dev_info(scd->cpu_dev, 
                     "Exiting boost: load=%u%% below exit threshold %u%%\n",
                     scd->avg_load, BOOST_EXIT_LOAD_THRESHOLD);
            should_boost = false;
        }
        
        /* Exit if temperature too high */
        if (!temp_ok) {
            dev_info(scd->cpu_dev, 
                     "Exiting boost: temperature %u°C too high\n",
                     scd->current_temp);
            should_boost = false;
        }
        
    } else {
        /* Not boosted - check entry conditions */
        u32 threshold = scd->gaming_mode ? 
                        GAMING_LOAD_BOOST_THRESHOLD : 
                        BOOST_ENTRY_LOAD_THRESHOLD;
        
        bool high_load = (scd->consecutive_high_load >= BOOST_CONSECUTIVE_SAMPLES);
        bool load_ok = (scd->avg_load >= threshold);
        bool gaming_ok = !scd->gaming_mode || 
                         (scd->gaming_mode && scd->avg_load >= 70);
        
        if (high_load && load_ok && temp_ok && not_throttled && 
            cooldown_ok && gaming_ok) {
            dev_info(scd->cpu_dev, 
                     "Entering boost: load=%u%% avg, %u consecutive samples\n",
                     scd->avg_load, scd->consecutive_high_load);
            should_boost = true;
        }
    }
    
    /* Apply boost decision */
    if (should_boost && !scd->boost_active) {
        /* Enter boost - find best boost OPP for current conditions */
        target_idx = OPP_BOOST_START_INDEX;
        
        /* Scale boost level based on load and temperature */
        if (scd->avg_load >= 95 && temp_ok) {
            target_idx = OPP_MAX_BOOST_INDEX;  // 3.9GHz only at max load
        } else if (scd->avg_load >= 85) {
            target_idx = 7;  // 3.8GHz
        } else if (scd->gaming_mode && scd->avg_load >= 70) {
            target_idx = OPP_BOOST_START_INDEX;  // 3.6GHz for gaming
        }
        
        /* Ensure target is within thermal limits */
        while (target_idx > OPP_DEFAULT_INDEX &&
               scd->current_temp > stargaze_opp_table[target_idx].thermal_ceiling) {
            target_idx--;
        }
        
        if (target_idx > scd->current_opp_idx) {
            int ret = stargaze_dvfs_transition(scd, target_idx);
            if (ret == 0) {
                scd->boost_active = true;
                scd->boost_entry_time = now;
                scd->boost_entries++;
                
                /* Set boost flag in hardware */
                u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
                ctrl |= DVFS_BOOST_ENABLE;
                cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
            }
            return ret;
        }
        
    } else if (!should_boost && scd->boost_active) {
        /* Exit boost - return to base frequency */
        int ret = stargaze_dvfs_transition(scd, OPP_DEFAULT_INDEX);
        if (ret == 0) {
            scd->boost_active = false;
            scd->last_boost_exit_time = now;
            
            /* Track boost duration */
            u64 boost_us = ktime_us_delta(now, scd->boost_entry_time);
            scd->boost_time_total_us += boost_us;
            
            /* Clear boost flag in hardware */
            u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
            ctrl &= ~DVFS_BOOST_ENABLE;
            cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
        }
        return ret;
    }
    
    return 0;
}

//==============================================================================
// Periodic Work Handlers
//==============================================================================

/*
 * Boost evaluation work - runs periodically to check boost conditions
 */
static void stargaze_boost_eval_worker(struct work_struct *work)
{
    struct stargaze_cpu_data *scd = 
        container_of(work, struct stargaze_cpu_data, boost_eval_work);
    
    /* Update load statistics */
    stargaze_update_load(scd);
    
    /* Detect gaming mode */
    bool was_gaming = scd->gaming_mode;
    scd->gaming_mode = stargaze_detect_gaming(scd);
    
    if (scd->gaming_mode && !was_gaming) {
        dev_info(scd->cpu_dev, "Gaming mode detected\n");
        
        /* Set gaming mode in hardware for optimizations */
        u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
        ctrl |= DVFS_GAMING_MODE;
        cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
        
        /* Apply gaming frequency strategy */
        stargaze_gaming_optimize(scd);
        
    } else if (!scd->gaming_mode && was_gaming) {
        dev_info(scd->cpu_dev, "Gaming mode ended\n");
        
        u32 ctrl = cmu_readl(scd, CMU_DVFS_CTRL);
        ctrl &= ~DVFS_GAMING_MODE;
        cmu_writel(scd, ctrl, CMU_DVFS_CTRL);
    }
    
    /* Evaluate boost conditions */
    if (scd->gaming_mode) {
        /* Gaming mode uses its own optimization */
        stargaze_gaming_optimize(scd);
    } else {
        /* Standard boost evaluation */
        stargaze_evaluate_boost(scd);
    }
    
    /* Reschedule */
    schedule_work(&scd->boost_eval_work);
}

/*
 * Thermal check work
 */
static void stargaze_thermal_check_worker(struct work_struct *work)
{
    struct stargaze_cpu_data *scd = 
        container_of(to_delayed_work(work), struct stargaze_cpu_data,
                     thermal_check_work);
    
    stargaze_thermal_check(scd);
    
    /* Reschedule based on temperature */
    unsigned long delay = msecs_to_jiffies(STARGAZE_THERMAL_POLL_MS);
    
    if (scd->current_temp >= STARGAZE_TEMP_THROTTLE) {
        delay = msecs_to_jiffies(25);  // Poll faster when hot
    }
    
    schedule_delayed_work(&scd->thermal_check_work, delay);
}

//==============================================================================
// cpufreq Driver Interface
//==============================================================================

static int stargaze_cpufreq_init(struct cpufreq_policy *policy)
{
    struct stargaze_cpu_data *scd;
    struct device *cpu_dev;
    int ret;
    
    cpu_dev = get_cpu_device(policy->cpu);
    if (!cpu_dev) {
        pr_err("%s: Failed to get CPU device\n", DRIVER_NAME);
        return -ENODEV;
    }
    
    scd = kzalloc(sizeof(*scd), GFP_KERNEL);
    if (!scd)
        return -ENOMEM;
    
    scd->policy = policy;
    scd->cpu_dev = cpu_dev;
    policy->driver_data = scd;
    
    /* Initialize mutex and completion */
    mutex_init(&scd->dvfs_lock);
    init_completion(&scd->transition_done);
    
    /* Map CMU registers */
    scd->cmu_base = ioremap(STARGZE_CMU_BASE, 0x1000);
    if (!scd->cmu_base) {
        dev_err(cpu_dev, "Failed to map CMU registers\n");
        ret = -ENOMEM;
        goto err_free;
    }
    
    /* Get clock and regulator */
    scd->cpu_clk = clk_get(cpu_dev, "cpu_clk");
    if (IS_ERR(scd->cpu_clk)) {
        ret = PTR_ERR(scd->cpu_clk);
        dev_err(cpu_dev, "Failed to get CPU clock: %d\n", ret);
        goto err_unmap;
    }
    
    scd->vreg_cpu = regulator_get(cpu_dev, "vdd_cpu");
    if (IS_ERR(scd->vreg_cpu)) {
        ret = PTR_ERR(scd->vreg_cpu);
        dev_err(cpu_dev, "Failed to get regulator: %d\n", ret);
        goto err_clk;
    }
    
    /* Set initial state to 3.4GHz base */
    scd->current_opp_idx = OPP_DEFAULT_INDEX;
    scd->current_freq_khz = stargaze_opp_table[OPP_DEFAULT_INDEX].freq_khz;
    scd->current_voltage_mv = stargaze_opp_table[OPP_DEFAULT_INDEX].voltage_mv;
    scd->boost_active = false;
    scd->gaming_mode = false;
    scd->thermal_throttle_active = false;
    scd->current_temp = 40;  // Assume moderate temp at boot
    scd->last_boost_exit_time = ktime_get();
    
    /* Set policy limits from OPP table */
    policy->cpuinfo.min_freq = stargaze_opp_table[OPP_MIN_INDEX].freq_khz;
    policy->cpuinfo.max_freq = stargaze_opp_table[OPP_MAX_BOOST_INDEX].freq_khz;
    policy->cpuinfo.transition_latency = 5000;  // 5us worst case
    
    policy->min = stargaze_opp_table[OPP_MIN_INDEX].freq_khz;
    policy->max = stargaze_opp_table[OPP_MAX_BOOST_INDEX].freq_khz;
    policy->cur = scd->current_freq_khz;
    
    /* All CPUs in cluster share policy */
    cpumask_copy(policy->cpus, cpu_coregroup_mask(policy->cpu));
    
    /* Build frequency table */
    scd->freq_table = kcalloc(OPP_COUNT + 1, sizeof(*scd->freq_table),
                              GFP_KERNEL);
    if (!scd->freq_table) {
        ret = -ENOMEM;
        goto err_regulator;
    }
    
    for (int i = 0; i < OPP_COUNT; i++) {
        scd->freq_table[i].frequency = stargaze_opp_table[i].freq_khz;
        scd->freq_table[i].driver_data = i;
    }
    scd->freq_table[OPP_COUNT].frequency = CPUFREQ_TABLE_END;
    
    policy->freq_table = scd->freq_table;
    
    /* Initialize work queues */
    INIT_WORK(&scd->boost_eval_work, stargaze_boost_eval_worker);
    INIT_DELAYED_WORK(&scd->thermal_check_work, stargaze_thermal_check_worker);
    
    /* Start periodic workers */
    schedule_work(&scd->boost_eval_work);
    schedule_delayed_work(&scd->thermal_check_work,
                          msecs_to_jiffies(STARGAZE_THERMAL_POLL_MS));
    
    /* Set initial voltage */
    stargaze_set_voltage(scd, scd->current_voltage_mv);
    
    dev_info(cpu_dev, "Stargaze X1 140K cpufreq driver v%s initialized\n",
             DRIVER_VERSION);
    dev_info(cpu_dev, "Base: %u MHz, Boost: %u MHz, Default: %u MHz\n",
             stargaze_opp_table[OPP_DEFAULT_INDEX].freq_khz / 1000,
             stargaze_opp_table[OPP_MAX_BOOST_INDEX].freq_khz / 1000,
             scd->current_freq_khz / 1000);
    dev_info(cpu_dev, "Voltage: %u mV, Boost threshold: %u%% load\n",
             scd->current_voltage_mv, BOOST_ENTRY_LOAD_THRESHOLD);
    dev_info(cpu_dev, "Thermal limits: warn=%u°C, throttle=%u°C, crit=%u°C\n",
             STARGAZE_TEMP_WARNING, STARGAZE_TEMP_THROTTLE, 
             STARGAZE_TEMP_CRITICAL);
    
    return 0;
    
err_regulator:
    regulator_put(scd->vreg_cpu);
err_clk:
    clk_put(scd->cpu_clk);
err_unmap:
    iounmap(scd->cmu_base);
err_free:
    kfree(scd);
    return ret;
}

static int stargaze_cpufreq_set_target(struct cpufreq_policy *policy,
                                        unsigned int target_freq,
                                        unsigned int relation)
{
    struct stargaze_cpu_data *scd = policy->driver_data;
    int target_idx = -1;
    
    if (scd->thermal_throttle_active) {
        /* During thermal throttle, respect hardware limits */
        target_freq = min(target_freq, 
                         stargaze_opp_table[scd->current_opp_idx].freq_khz);
    }
    
    /* Find closest OPP to target */
    if (relation == CPUFREQ_RELATION_L) {
        /* Lowest frequency at or above target */
        for (int i = 0; i < OPP_COUNT; i++) {
            if (stargaze_opp_table[i].freq_khz >= target_freq) {
                target_idx = i;
                break;
            }
        }
    } else {
        /* Highest frequency at or below target */
        for (int i = OPP_COUNT - 1; i >= 0; i--) {
            if (stargaze_opp_table[i].freq_khz <= target_freq) {
                target_idx = i;
                break;
            }
        }
    }
    
    if (target_idx < 0) {
        target_idx = OPP_DEFAULT_INDEX;
    }
    
    /* Skip if already at target */
    if (target_idx == scd->current_opp_idx) {
        return 0;
    }
    
    dev_dbg(scd->cpu_dev, "cpufreq set: %u kHz → %u kHz (OPP %d)\n",
            scd->current_freq_khz, 
            stargaze_opp_table[target_idx].freq_khz,
            target_idx);
    
    return stargaze_dvfs_transition(scd, target_idx);
}

static unsigned int stargaze_cpufreq_get(unsigned int cpu)
{
    struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
    struct stargaze_cpu_data *scd;
    unsigned int freq;
    
    if (!policy)
        return 0;
    
    scd = policy->driver_data;
    freq = scd->current_freq_khz;
    
    cpufreq_cpu_put(policy);
    return freq;
}

static int stargaze_cpufreq_verify(struct cpufreq_policy_data *policy)
{
    cpufreq_verify_within_cpu_limits(policy);
    return 0;
}

//==============================================================================
// Sysfs Interface
//==============================================================================

static ssize_t boost_show(struct cpufreq_policy *policy, char *buf)
{
    struct stargaze_cpu_data *scd = policy->driver_data;
    return sprintf(buf, "%u\n", scd->boost_active);
}

static ssize_t boost_store(struct cpufreq_policy *policy,
                           const char *buf, size_t count)
{
    struct stargaze_cpu_data *scd = policy->driver_data;
    int enable, ret;
    
    ret = kstrtoint(buf, 10, &enable);
    if (ret)
        return ret;
    
    if (enable && !scd->boost_active) {
        ret = stargaze_dvfs_transition(scd, OPP_BOOST_START_INDEX);
        if (ret == 0) {
            scd->boost_active = true;
            scd->boost_entry_time = ktime_get();
        }
    } else if (!enable && scd->boost_active) {
        ret = stargaze_dvfs_transition(scd, OPP_DEFAULT_INDEX);
        if (ret == 0) {
            scd->boost_active = false;
            scd->last_boost_exit_time = ktime_get();
        }
    }
    
    return ret ? ret : count;
}

static ssize_t stats_show(struct cpufreq_policy *policy, char *buf)
{
    struct stargaze_cpu_data *scd = policy->driver_data;
    int len = 0;
    
    len += sprintf(buf + len, "Current OPP: %s (%u MHz, %u mV)\n",
                   stargaze_opp_table[scd->current_opp_idx].name,
                   scd->current_freq_khz / 1000,
                   scd->current_voltage_mv);
    len += sprintf(buf + len, "Boost: %s\n", 
                   scd->boost_active ? "active" : "inactive");
    len += sprintf(buf + len, "Gaming mode: %s\n",
                   scd->gaming_mode ? "active" : "inactive");
    len += sprintf(buf + len, "Temperature: %u°C\n", scd->current_temp);
    len += sprintf(buf + len, "Load: %u%% avg, %u%% peak\n",
                   scd->avg_load, scd->peak_load);
    len += sprintf(buf + len, "Throttle: %s (level %d)\n",
                   scd->thermal_throttle_active ? "active" : "inactive",
                   scd->throttle_level);
    len += sprintf(buf + len, "Transitions: %llu\n", scd->transition_count);
    len += sprintf(buf + len, "Boost entries: %llu\n", scd->boost_entries);
    len += sprintf(buf + len, "Thermal events: %llu\n", scd->throttle_events);
    
    return len;
}

static struct freq_attr stargaze_cpufreq_attr[] = {
    __ATTR(boost, 0644, boost_show, boost_store),
    __ATTR(stats, 0444, stats_show, NULL),
    { }
};

//==============================================================================
// cpufreq Driver
//==============================================================================

static struct cpufreq_driver stargaze_cpufreq_driver = {
    .name       = DRIVER_NAME,
    .flags      = CPUFREQ_STICKY | CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
                  CPUFREQ_NEED_INITIAL_FREQ_CHECK,
    .init       = stargaze_cpufreq_init,
    .verify     = stargaze_cpufreq_verify,
    .target     = stargaze_cpufreq_set_target,
    .get        = stargaze_cpufreq_get,
    .attr       = stargaze_cpufreq_attr,
};

//==============================================================================
// Platform Driver
//==============================================================================

static int stargaze_cpufreq_probe(struct platform_device *pdev)
{
    int ret;
    
    dev_info(&pdev->dev, "Stargaze X1 140K cpufreq driver probing\n");
    
    ret = cpufreq_register_driver(&stargaze_cpufreq_driver);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register cpufreq: %d\n", ret);
        return ret;
    }
    
    dev_info(&pdev->dev, "cpufreq driver registered successfully\n");
    
    return 0;
}

static int stargaze_cpufreq_remove(struct platform_device *pdev)
{
    cpufreq_unregister_driver(&stargaze_cpufreq_driver);
    return 0;
}

static const struct of_device_id stargaze_cpufreq_match[] = {
    { .compatible = "stargaze,x1-140k-cpufreq" },
    {}
};
MODULE_DEVICE_TABLE(of, stargaze_cpufreq_match);

static struct platform_driver stargaze_cpufreq_platform_driver = {
    .probe  = stargaze_cpufreq_probe,
    .remove = stargaze_cpufreq_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = stargaze_cpufreq_match,
    },
};

module_platform_driver(stargaze_cpufreq_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stargaze Semiconductor");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);