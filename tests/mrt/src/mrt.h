/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the MRT case needs from the hardware.
 *
 * An MRT is one register block with four independent down-counters, and the driver
 * registers one counter device per channel plus a module device that owns init, the
 * shared interrupt and the clock. So the device a consumer holds is a channel, while
 * the only thing a suspend can give back belongs to the module. Both devices are
 * named in @ref mrt_case for that reason.
 *
 * Three things are observed.
 *
 * The module's bus clock gate, read out of the clock controller rather than out of an
 * MRT register: an MRT register only answers while the block is clocked, and whether
 * it is clocked is the question. The MRT has exactly one clock, which both gates the
 * registers and runs the counters, so gating it is the whole of what a suspend does
 * and it stops every channel at once.
 *
 * The per-channel register image: CTRL holds the mode and the interrupt enable,
 * INTVAL holds the period, and STAT says whether the channel is running. Nothing else
 * in the block carries them. A suspend keeps the block powered, so the image it hands
 * back has to be the one it was given.
 *
 * A channel whose top callback keeps arriving, which is the control: it is what makes
 * "the gate is open" mean the peripheral works rather than that a bit is set.
 *
 * Two things this case deliberately does not claim.
 *
 * It makes no claim about a count value surviving anything. INTVAL is both the period
 * register and the start trigger -- writing a nonzero IVALUE to an idle channel starts
 * it immediately -- so there is no way to restore a period without also restarting a
 * run the consumer did not ask for, and the driver does not try. Across a power cycle
 * the assertion is behavioural instead: a channel started fresh after the round trip
 * has to produce callbacks, which it cannot unless the module's turn-on re-ran the
 * bring-up and re-connected the interrupt.
 *
 * It does not read the period back through counter_get_top_value(). That call answers
 * from the driver's own RAM copy, by design, so it would agree with itself whatever
 * the register held. The register image below is read straight out of the block.
 */

#ifndef MRT_PM_H_
#define MRT_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/counter.h>

#if !DT_HAS_COMPAT_STATUS_OKAY(nxp_mrt_channel)
#error "No nxp,mrt-channel node is enabled on this board"
#endif

#if !defined(CONFIG_COUNTER_NXP_MRT)
#error "CONFIG_COUNTER_NXP_MRT is not enabled, so there is no driver to test"
#endif

/**
 * The period every test programs, in microseconds.
 *
 * Short enough that a handful of callbacks arrive inside a test, and far enough
 * inside the 24-bit counter that a fast bus clock still fits: at 150 MHz, the
 * highest this driver's parts run, 10 ms is 1.5 million of the 16.7 million ticks
 * available.
 */
#define MRT_PERIOD_US 10000U

/** How many enabled nxp,mrt-channel devices this build covers. */
#define MRT_PM_COUNT_ONE(node_id) +1
#define MRT_CASE_COUNT (0 DT_FOREACH_STATUS_OKAY(nxp_mrt_channel, MRT_PM_COUNT_ONE))

/** One MRT channel under test. */
struct mrt_case {
	/** The counter device: a channel. This is what a consumer holds. */
	const struct device *dev;
	/** The nxp,mrt module device: owns the reset, the interrupt and the clock. */
	const struct device *module;
	/** Channel index within the module, from the node's reg. */
	uint8_t channel;
	/** The module's register block, shared by its channels. */
	uintptr_t base;
};

extern const struct mrt_case mrt_cases[MRT_CASE_COUNT];

/** @brief Iterate the channels. */
#define MRT_FOREACH_CASE(c)                                                                        \
	for (const struct mrt_case *c = &mrt_cases[0]; c < &mrt_cases[MRT_CASE_COUNT]; c++)

/**
 * @brief True when this SoC's HAL names a clock gate for the MRT module.
 *
 * A module whose gate cannot be named is reported and asserted nothing about, rather
 * than passing quietly.
 */
bool mrt_gate_observable(void);

/** @brief True while the module's bus clock gate is open. */
bool mrt_gate_is_open(void);

/** @brief Open the gate behind the driver's back, so a clobber can be written. */
void mrt_gate_open(void);

/** @brief Close it again, leaving the block as a suspend left it. */
void mrt_gate_close(void);

/**
 * @brief The registers one channel's configuration lives in.
 *
 * Only meaningful while the block is clocked; every caller here reads it with the
 * gate open.
 */
struct mrt_image {
	/** CTRL: the operating mode and the channel interrupt enable. */
	uint32_t ctrl;
	/** INTVAL[IVALUE]: the period. Reads zero while the channel is idle. */
	uint32_t intval;
	/** STAT[RUN]: set while the channel is counting. */
	bool running;
};

/** @brief Sample @ref mrt_image for one channel. */
void mrt_image_read(const struct mrt_case *c, struct mrt_image *img);

/** @brief True when two images are the same. */
bool mrt_image_equal(const struct mrt_image *a, const struct mrt_image *b);

/**
 * @brief Clear one channel's CTRL, so a restore has something to put back.
 *
 * What stands in for a power cycle in a layer where nothing really cut power. CTRL
 * is the register the module's turn-on action rewrites; INTVAL is deliberately left
 * alone, because writing it would start the channel. Takes care of the clock gate
 * itself and leaves it as it found it.
 *
 * @param c         channel to clobber
 * @param clobbered read back after the write, so a caller can prove the clobber took
 */
void mrt_clobber(const struct mrt_case *c, struct mrt_image *clobbered);

/** @brief True while STAT[RUN] says this channel is counting. */
bool mrt_running(const struct mrt_case *c);

/**
 * @brief Program @ref MRT_PERIOD_US on @p c and start it.
 *
 * Clears the callback count first. The top value is stored by the driver while the
 * channel is idle and written to INTVAL by the start, which is also what begins the
 * count.
 *
 * @return 0, or negative errno from the counter API.
 */
int mrt_start(const struct mrt_case *c);

/** @brief counter_stop(). */
int mrt_stop(const struct mrt_case *c);

/** @brief How many top callbacks this channel has delivered since the last start. */
uint32_t mrt_ticks(const struct mrt_case *c);

/**
 * @brief Wait up to @p timeout_ms for @p want further callbacks on @p c.
 *
 * @return true if that many arrived within the timeout.
 */
bool mrt_wait_ticks(const struct mrt_case *c, uint32_t want, uint32_t timeout_ms);

/** @brief True when two reads of TIMER a short wait apart differ. */
bool mrt_advances(const struct mrt_case *c);

/** @brief Print what the devicetree asked for, once, from the suite setup. */
void mrt_report_configuration(void);

/**
 * @brief Print the gate and, when the gate is open, the channel's register image.
 *
 * Safe to call with the gate closed: it prints the gate and says the registers were
 * not read rather than faulting on an unclocked block.
 *
 * @param when short label for where in the sequence this is, e.g. "before".
 */
void mrt_report_hw(const struct mrt_case *c, const char *when);

#endif /* MRT_PM_H_ */
