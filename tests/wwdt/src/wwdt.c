/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The WWDT wiring, and the hardware truths this case asserts on.
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/tc_util.h>

#include <fsl_clock.h>
#include <soc.h>

#include "wwdt.h"

/*
 * The gate below is read out of the MCX clock controller, so a part whose WWDT
 * takes its clock from some other controller is refused rather than read out of
 * the wrong register.
 */
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(WWDT_NODE), nxp_lpc_syscon),
             "this case reads the WWDT clock gate out of the MCX clock "
             "controller and only "
             "fits SoCs whose WWDT clock comes from nxp,lpc-syscon");

#if defined(WWDT_MOD_LOCK_MASK)
#define WWDT_MOD_CONFIG_LOCK_MASK WWDT_MOD_LOCK_MASK
#else
#define WWDT_MOD_CONFIG_LOCK_MASK 0U
#endif

#define WWDT_MOD_CONFIG_MASK                                                   \
  (WWDT_MOD_WDEN_MASK | WWDT_MOD_WDRESET_MASK | WWDT_MOD_WDPROTECT_MASK |      \
   WWDT_MOD_CONFIG_LOCK_MASK)

const struct device *const wwdt_dev = DEVICE_DT_GET(WWDT_NODE);

int wwdt_start_rc = -EINPROGRESS;

static volatile bool wwdt_fired;
static bool wwdt_installed;

static WWDT_Type *wwdt_base(void) {
  return (WWDT_Type *)DT_REG_ADDR(WWDT_NODE);
}

/*
 * CLOCK_EnableClock()/CLOCK_DisableClock() write through the write-only SET/CLR
 * aliases, so the readable register is the one at the head of each group:
 * SYSCON->AHBCLKCTRL0..3 on MCXN, MRCC0->MRCC_GLB_CCn on MCXA.
 */
bool wwdt_gate_is_open(void) {
#if defined(CONFIG_SOC_FAMILY_MCXA)
  const clock_ip_name_t gate = kCLOCK_GateWWDT0;
  const volatile uint32_t *reg =
      (const volatile uint32_t *)((uintptr_t)&MRCC0->MRCC_GLB_CC0 +
                                  CLK_GATE_REG_OFFSET(gate));

  return (*reg & BIT(CLK_GATE_BIT_SHIFT(gate))) != 0U;
#else
  const clock_ip_name_t gate = kCLOCK_Wwdt0;
  const volatile uint32_t *reg =
      &(&SYSCON->AHBCLKCTRL0)[CLK_GATE_ABSTRACT_REG_OFFSET(gate)];

  return (*reg & BIT(CLK_GATE_ABSTRACT_BITS_SHIFT(gate))) != 0U;
#endif
}

void wwdt_image_read(struct wwdt_image *img) {
  WWDT_Type *base = wwdt_base();

  img->mod = base->MOD & WWDT_MOD_CONFIG_MASK;
  img->tc = base->TC;
  img->window = base->WINDOW;
  img->warnint = base->WARNINT;
}

bool wwdt_clock_is_halted(void) {
#if defined(CONFIG_SOC_FAMILY_MCXA)
  return CLOCK_GetClockDiv(kCLOCK_DivWWDT0) == 0U;
#else
  return CLOCK_GetClkDiv(kCLOCK_DivWdt0Clk) == 0U;
#endif
}

bool wwdt_image_equal(const struct wwdt_image *a, const struct wwdt_image *b) {
  return (a->mod == b->mod) && (a->tc == b->tc) && (a->window == b->window) &&
         (a->warnint == b->warnint);
}

bool wwdt_image_enabled(const struct wwdt_image *img) {
  return (img->mod & WWDT_MOD_WDEN_MASK) != 0U;
}

bool wwdt_image_resets(const struct wwdt_image *img) {
  return (img->mod & WWDT_MOD_WDRESET_MASK) != 0U;
}

uint32_t wwdt_tv(void) { return wwdt_base()->TV; }

bool wwdt_counting(void) {
  uint32_t first = wwdt_tv();

  /*
   * Busy-waited rather than slept: 2 ms is hundreds of counts at any WWDT clock
   * a board here attaches, and a sleep would let the idle thread enter a power
   * state in the layers that have one.
   */
  k_busy_wait(2000);

  return wwdt_tv() < first;
}

int wwdt_feed_and_verify(void) {
  uint32_t before;
  uint32_t after;
  int err;

  k_busy_wait(2000);
  before = wwdt_tv();

  err = wdt_feed(wwdt_dev, 0);
  if (err != 0) {
    return err;
  }

  /* A feed reaches the counter a few WDCLK cycles later. */
  k_busy_wait(100);
  after = wwdt_tv();

  return (after > before) ? 0 : -ESTALE;
}

bool wwdt_expired(void) { return wwdt_fired; }

static void wwdt_callback(const struct device *dev, int channel_id) {
  ARG_UNUSED(dev);
  ARG_UNUSED(channel_id);

  wwdt_fired = true;
}

int wwdt_install(void) {
  const struct wdt_timeout_cfg cfg = {
      .window = {.min = 0U, .max = WWDT_TIMEOUT_MS},
      .callback = wwdt_callback,
      /* Interrupt only: nothing this case does may reset the board. */
      .flags = WDT_FLAG_RESET_NONE,
  };
  int ret;

  if (wwdt_installed) {
    return 0;
  }

  ret = wdt_install_timeout(wwdt_dev, &cfg);
  if (ret < 0) {
    return ret;
  }

  wwdt_installed = true;

  return 0;
}

__weak int wwdt_layer_before_start(void) { return 0; }

__weak void wwdt_layer_after_stop(void) {}

int wwdt_seed_idle_registers(void) {
  if (!wwdt_gate_is_open() || ((wwdt_base()->MOD & WWDT_MOD_WDEN_MASK) != 0U)) {
    return -EBUSY;
  }
  wwdt_base()->TC = 0x12345U;
  return wwdt_base()->TC == 0x12345U ? 0 : -EIO;
}

int wwdt_start(void) {
  int ret = wwdt_install();

  if (ret != 0) {
    return ret;
  }

  return wdt_setup(wwdt_dev, 0);
}

void wwdt_report_configuration(void) {
  TC_PRINT("%s: WWDT @ %p, clk-divider %u, timeout %u ms, reset none\n",
           wwdt_dev->name, (void *)wwdt_base(),
           (unsigned int)DT_PROP(WWDT_NODE, clk_divider), WWDT_TIMEOUT_MS);
}

void wwdt_report_hw(const char *when) {
  struct wwdt_image img;

  if (!wwdt_gate_is_open()) {
    /* No register read: an unclocked block does not answer. */
    TC_PRINT("hw(%s) gate=0, registers not read\n", when);
    return;
  }

  wwdt_image_read(&img);
  TC_PRINT("hw(%s) gate=1 MOD=0x%02x TC=0x%06x WINDOW=0x%06x WARNINT=0x%03x "
           "TV=0x%06x\n",
           when, img.mod, img.tc, img.window, img.warnint, wwdt_tv());
}
