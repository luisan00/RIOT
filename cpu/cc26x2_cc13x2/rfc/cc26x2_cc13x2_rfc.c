/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup         cpu_cc26x2_cc13x2
 * @{
 *
 * @file
 * @brief           CC26x2/CC13x2 RF Core common functions
 *
 * The sequences in this file are checked against these documents from
 * Texas Instruments:
 *
 * - [TRM]: CC13x2, CC26x2 SimpleLink Wireless MCU Technical Reference
 *   Manual, SWCU185G (June 2024), chapters 7 (PRCM), 16 (RTC) and 25 (Radio).
 * - [SDK]: SimpleLink CC13xx/CC26xx SDK 8.33.00.16. The RF driver is
 *   `source/ti/drivers/rf/RFCC26X2_multiMode.c` and driverlib is
 *   `source/ti/devices/cc13x2_cc26x2/driverlib`.
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 * @}
 */

#include <assert.h>
#include <errno.h>

#include "cpu.h"
#include "cc26x2_cc13x2_rfc.h"
#include "cc26xx_cc13xx_power.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/**
 * @brief   Polls of the command status after each CMD_ABORT
 *
 * A command that reacts to CMD_ABORT has its final status by the first poll.
 * 1000 polls take about 0.35 ms at 48 MHz.
 */
#define ABORT_POLL_MAX      (1000U)

/**
 * @brief   Number of times CMD_ABORT is sent before giving up
 */
#define ABORT_ATTEMPTS      (16U)

/**
 * @brief   Number of times CMD_STOP is sent before the command is taken as
 *          busy with a packet
 *
 * A receive command that is searching for sync ends at the first one, and
 * its final status is there by the first poll. The second one covers a
 * CMD_STOP without effect, as it was seen with CMD_ABORT right after the
 * start of the command, see cc26x2_cc13x2_rfc_abort_cmd(). A command that is
 * busy with a packet is therefore known after two rounds of polls, about
 * 0.7 ms. Measured on a CC1352P1.
 */
#define STOP_ATTEMPTS       (2U)

/*
 * RF Core power-up, first half: from "off" to "CPE booting".
 *
 * The steps are the ones of RF_fsmPowerUpState() in [SDK]:
 *
 * 1. Select the command set in PRCM:RFCMODESEL ([TRM] Table 7-92).
 * 2. Power up the RF Core domain and wait until it is on.
 * 3. Set up the CPE interrupts in the doorbell.
 * 4. Start the CPE by enabling its clocks in RFC_PWR:PWMCLKEN.
 *
 * Differences with [SDK]:
 *
 * - [SDK] also requests XOSC_HF here, so the crystal starts while the CPE
 *   boots. This driver switches to XOSC_HF later, in
 *   cc26x2_cc13x2_rfc_finish_on().
 * - [SDK] enables BOOT_DONE in RFCPEIEN and continues from the RF_CPE0
 *   interrupt. This driver polls RFCPEIFG in cc26x2_cc13x2_rfc_confirm_on().
 * - [SDK] always starts from a powered-off RF Core domain. This driver may
 *   find it on, see below.
 */
void cc26x2_cc13x2_rfc_request_on(void)
{
    /* The CPE only boots, and only raises BOOT_DONE, when its clocks are
     * enabled after the RF Core domain was off. The domain is still on if the
     * system CPU was reset alone, as a debugger does with a core-only reset:
     * BOOT_DONE would never come and the CPE would keep its old state.
     * Switching the domain off and on is the documented way to reset the CPE
     * ([TRM] Table 25-207, INTERNAL_ERROR). */
    if (power_is_domain_enabled(POWER_DOMAIN_RFC)) {
        NVIC_DisableIRQ(RF_CPE0_IRQN);
        NVIC_DisableIRQ(RF_CPE1_IRQN);

        /* the RF Core registers need this clock gate */
        power_clock_enable_rfc();
        RFC_DBELL_NONBUF->RFCPEIEN = 0;
        /* RFCClockDisable() in [SDK] rfc.h */
        RFC_PWR_NONBUF->PWMCLKEN = 0;

        power_disable_domain(POWER_DOMAIN_RFC);
    }

    /* [SDK] writes RF_Mode::rfMode here. Every RF_MODE_* value of this family
     * is 0x00 in driverlib (rf_mailbox.h), so 0 is the documented value for
     * the proprietary mode too. On cc13x0 one of the modes of
     * PRCM->RFCMODEHWOPT must be written instead. */
    PRCM->RFCMODESEL = 0;

    /* PRCMPowerDomainOn(PRCM_DOMAIN_RFCORE) followed by a wait on
     * PRCMPowerDomainsAllOn() in [SDK] prcm.c. power_enable_domain() also
     * sets PDCTL1RFC, which [SDK] leaves alone on this family, see
     * power_arch.c. */
    power_enable_domain(POWER_DOMAIN_RFC);

    /* PRCM:RFCCLKG.CLK_EN resets to 1 ([TRM] Table 7-34) and [SDK] never
     * writes it, so this only matters if something else cleared it. */
    power_clock_enable_rfc();

    /* The doorbell can be accessed from here on, before PWMCLKEN is written:
     * PWMCLKEN.RFC, the clock of the doorbell, resets to 1 and is read-only
     * ([TRM] Table 25-214).
     *
     * RFCPEIFG flags are cleared by writing 0 to them ([TRM] 25.2.3.1). */
    RFC_DBELL_NONBUF->RFCPEIFG = 0;
    RFC_DBELL_NONBUF->RFCPEIEN = 0;

    /* RFCPEISL routes each CPE interrupt to RF_CPE0 (bit = 0) or to RF_CPE1
     * (bit = 1), see [TRM] 25.2.3.1. Only INTERNAL_ERROR goes to RF_CPE1.
     * [SDK] routes everything to RF_CPE0 (RFCCpe0IntSelect()). */
    RFC_DBELL_NONBUF->RFCPEISL = 0;
    RFC_DBELL_NONBUF->RFCPEISL |= CPE_IRQ_INTERNAL_ERROR;

    NVIC_ClearPendingIRQ(RF_CPE0_IRQN);
    NVIC_ClearPendingIRQ(RF_CPE1_IRQN);
    NVIC_EnableIRQ(RF_CPE0_IRQN);
    NVIC_EnableIRQ(RF_CPE1_IRQN);

    /* Enable internal error interrupt (handled on isr_rfc_cpe1) */
    RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_INTERNAL_ERROR;

    /* Same value as RFCClockEnable() in [SDK] rfc.h. [TRM] Table 25-214: CPE
     * and CPERAM must be set together to let the CPE boot. The other clocks
     * (modem, RF engine, RAT, ...) are handled by the CPE itself. */
    RFC_PWR_NONBUF->PWMCLKEN = PWMCLKEN_CPERAM | PWMCLKEN_CPE | PWMCLKEN_RFC;
}

/*
 * The CPE reports its boot with two RFCPEIFG flags ([TRM] Table 25-207):
 *
 * - MODULES_UNLOCKED (bit 29): raised during the boot, when the CPE has
 *   opened access to the RF Core modules and memories.
 * - BOOT_DONE (bit 30): the boot is finished.
 *
 * As [SDK] (RF_hwiCpe0PowerFsm()), this function only continues on BOOT_DONE.
 */
int cc26x2_cc13x2_rfc_confirm_on(void)
{
    if (!(RFC_DBELL->RFCPEIFG & CPE_IRQ_BOOT_DONE)) {
        DEBUG("[cc26x2_cc13x2_rfc]: boot unfinished, RFCPEIFG=%08lx\n",
              RFC_DBELL->RFCPEIFG);
        return -EAGAIN;
    }

    DEBUG("[cc26x2_cc13x2_rfc]: boot done, RFCPEIFG=%08lx\n",
          RFC_DBELL->RFCPEIFG);

    /* Write the inverted mask, never `RFCPEIFG &= ~flag`: a read-modify-write
     * can lose a flag that is raised between the read and the write
     * ([TRM] 25.2.3.1, note). */
    RFC_DBELL_NONBUF->RFCPEIFG = ~(CPE_IRQ_MODULES_UNLOCKED | CPE_IRQ_BOOT_DONE);

    return 0;
}

/*
 * RF Core power-up, second half: from "CPE booted" to "ready for the setup
 * command chain". [SDK] does this in RF_fsmSetupState().
 *
 * [SDK] also sends the direct command CMD_BUS_REQUEST(1) at this point
 * ([TRM] 25.3.3.2.20). It keeps the system bus awake in deep sleep, which the
 * RF Core needs to reach command structures and buffers in system RAM. This
 * driver does not send it. RIOT only uses sleep (WFI) on this CPU; the
 * command is needed if a standby mode is ever used with the radio on.
 */
void cc26x2_cc13x2_rfc_finish_on(void (* cpe_patch_fn)(void))
{
    DEBUG("[cc26x2_cc13x2_rfc]: changing HF osc to XOSC\n");
    /* [TRM] 25.2.4.3: the system must run from the HF crystal oscillator
     * from before CMD_SYNC_START_RAT until after CMD_SYNC_STOP_RAT.
     *
     * osc_hf_source_switch() is OSCClockSourceSet(), a wait on
     * OSCHfSourceReady() and OSCHfSourceSwitch() of [SDK] osc.[ch]. It blocks
     * until the crystal is stable. [SDK] does not block: it runs the setup
     * command first and polls the oscillator before CMD_SYNC_START_RAT
     * (RF_fsmXOSCState()). */
    osc_hf_source_switch(OSC_XOSC_HF);

    /* The CPE patch is applied after BOOT_DONE and before any command, as
     * RF_applyRfCorePatch(RF_PHY_BOOTUP_MODE) does in [SDK]. The proprietary
     * PHYs used here need no MCE or RFE patch.
     *
     * TI's patch functions copy the patch image only the first time they
     * are called after a reset of the MCU, and set the patch table every
     * time. The image is still there after a power cycle of the RF Core
     * because it lives in the CPE ULL RAM, whose retention is on by default
     * (PRCM:RAMRETEN.RFCULL, [TRM] Table 7-96). */
    if (cpe_patch_fn != NULL) {
        DEBUG("[cc26x2_cc13x2_rfc]: patching CPE\n");
        cpe_patch_fn();
    }

    DEBUG("[cc26x2_cc13x2_rfc]: turning AON_RTC clock line ON\n");
    /* CMD_SYNC_START_RAT and CMD_SYNC_STOP_RAT need AON_RTC:CTL.RTC_UPD_EN
     * set ([TRM] 25.2.4.3, note). RTC_UPD is SCLK_LF divided by 2 (16 kHz)
     * and does not depend on CTL.EN, so the RTC counter itself does not have
     * to run ([TRM] Table 16-3). The bit never has to be cleared again. */
    AON_RTC->CTL |= AON_RTC_CTL_RTC_UPD_EN;
}

/*
 * RF Core power-down, after the radio commands that prepare it (see
 * cc26x2_cc13x2_rf_off()). [SDK] does the same at the end of
 * RF_corePowerDown() and in the power state machine after it:
 *
 * 1. Disable every CPE and hardware interrupt of the RF Core
 *    (RFCCpeIntDisable(~0), RFCHwIntDisable(~0)). Their flags are lost with
 *    the power domain anyway.
 * 2. Stop every clock of the RF Core: RFCClockDisable() writes 0 to
 *    RFC_PWR:PWMCLKEN.
 * 3. Switch the power domain off (Power_releaseDependency() of the RF Core
 *    domain). Both PDCTL0RFC and PDCTL1RFC are cleared, see
 *    power_disable_domain().
 * 4. Release the HF crystal oscillator: nothing else in RIOT asks for it on
 *    this CPU, so the system goes back to the HF RC oscillator.
 */
void cc26x2_cc13x2_rfc_power_off(void)
{
    NVIC_DisableIRQ(RF_CPE0_IRQN);
    NVIC_DisableIRQ(RF_CPE1_IRQN);

    if (power_is_domain_enabled(POWER_DOMAIN_RFC)) {
        RFC_DBELL_NONBUF->RFCPEIEN = 0;
        RFC_DBELL_NONBUF->RFHWIEN = 0;
        RFC_PWR_NONBUF->PWMCLKEN = 0;

        power_disable_domain(POWER_DOMAIN_RFC);
    }

    osc_hf_source_switch(OSC_RCOSC_HF);
}

/**
 * @brief   Send a command synchronously to the RF Core
 *
 * This is the doorbell protocol of [TRM] 25.2.2 and 25.3.2.2, with the same
 * steps as RFCDoorbellSendTo() in [SDK] rfc.c:
 *
 * - CMDR can only be written while it reads 0, other writes are ignored.
 * - Writing CMDR sets CMDSTA to 0 (Pending).
 * - When the radio CPU has parsed the command, CMDR goes back to 0, CMDSTA
 *   gets the result and RFACKIFG.ACKFLAG is set. No new command may be issued
 *   before that.
 *
 * The result is in the low byte of CMDSTA ([TRM] Table 25-2). 0x01 (Done)
 * means "finished" for an immediate or direct command, but only "accepted"
 * for a radio operation command. A radio operation ends later, with
 * COMMAND_DONE or LAST_COMMAND_DONE in RFCPEIFG and a final value in the
 * status field of its structure.
 *
 * @param[in] cmd Pointer to a command structure, 32-bit aligned, or a direct
 *                command: ID in bits 31-16, optional parameter in bits 15-8
 *                and 01 in the two LSBs ([TRM] Figures 25-3 and 25-4).
 */
uint32_t cc26x2_cc13x2_rfc_request_execute(uintptr_t cmd)
{
    assert((cmd & 3) == 0 || (cmd & 0x00000001));

    unsigned key = irq_disable();
    /* Wait until the doorbell becomes available */
    while (RFC_DBELL->CMDR != 0) {}
    RFC_DBELL->RFACKIFG = 0;

    /* Submit the command to the CM0 through the doorbell */
    RFC_DBELL->CMDR = cmd;

    /* Wait until the CM0 starts to parse the command */
    while (!RFC_DBELL->RFACKIFG) {}
    RFC_DBELL->RFACKIFG = 0;

    /* Return the content of status register */
    uint32_t cmdsta = RFC_DBELL->CMDSTA;
    irq_restore(key);

    return cmdsta;
}

/*
 * LAST_COMMAND_DONE (RFCPEIFG bit 1) is raised when the last radio operation
 * command of a chain has finished ([TRM] Table 25-207). It is also raised
 * when the chain ends with an error, so the result must be read from the
 * status field of the commands ([TRM] Tables 25-3 and 25-182).
 */
int cc26x2_cc13x2_rfc_confirm_execute(void)
{
    if (!(RFC_DBELL->RFCPEIFG & CPE_IRQ_LAST_COMMAND_DONE)) {
        return -EAGAIN;
    }

    RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;

    return 0;
}

/*
 * There are two direct commands to end a radio operation:
 *
 * - CMD_STOP (0x0402) asks it to stop gracefully. A packet being received or
 *   transmitted is normally handled to completion, and the operation ends
 *   with DONE_STOPPED ([TRM] 25.3.3.2.2). At 50 kbit/s a frame can take
 *   hundreds of milliseconds, too long to wait for here.
 * - CMD_ABORT (0x0401) ends it as soon as possible, with DONE_ABORT
 *   ([TRM] 25.3.3.2.1). This is the one used.
 *
 * For both commands CMDSTA is Done in all cases and "may be updated before
 * the radio operation ends". Until the operation has really ended a new radio
 * operation command is rejected with SchedulingError (0x86, [TRM] Table 25-2)
 * and its command structure must not be modified ([TRM] 25.3.2.2). So this
 * function waits until the status field of the command gets a final value.
 * The radio CPU writes the final status before it raises COMMAND_DONE and
 * LAST_COMMAND_DONE ([TRM] 25.3.2.5), so those two flags can still be raised
 * shortly after this function returns.
 *
 * CMD_ABORT is sent again if the command does not end. This is not from
 * [TRM], it was measured on CC1352P1 LaunchPads: a CMD_ABORT sent right after
 * CMD_PROP_RX_ADV was accepted gets CMDSTA Done and has no effect, the RX
 * command keeps running. A second CMD_ABORT one round of polls later ends it
 * at once. In every other case the status was final at the first poll.
 */
int cc26x2_cc13x2_rfc_abort_cmd(const volatile uint16_t *status)
{
    assert(status != NULL);

    for (unsigned attempt = 0; attempt < ABORT_ATTEMPTS; attempt++) {
        if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
            return 0;
        }

        uint32_t cmdsta =
            cc26x2_cc13x2_rfc_request_execute(RFC_CMDR_DIR_CMD(RFC_CMD_ABORT));
        if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
            DEBUG("[cc26x2_cc13x2_rfc]: CMD_ABORT failed, cmdsta=%08lx\n",
                  cmdsta);
            return -EIO;
        }

        for (unsigned i = 0; i < ABORT_POLL_MAX; i++) {
            if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
                return 0;
            }
        }
    }

    DEBUG("[cc26x2_cc13x2_rfc]: command still running after CMD_ABORT\n");
    return -ETIMEDOUT;
}

/*
 * Ends a radio operation command with CMD_STOP, the graceful one of the two
 * direct commands described at cc26x2_cc13x2_rfc_abort_cmd(). A receive
 * command ends at once while it is searching for sync, and at the end of the
 * packet if it has found sync: "Received CMD_STOP after command started and,
 * if sync found, packet is received" ([TRM] Table 25-186). The status is
 * PROP_DONE_STOPPED in both cases.
 *
 * So a command that has not ended shortly after CMD_STOP is receiving a
 * packet. It keeps the request and ends by itself after the packet.
 */
int cc26x2_cc13x2_rfc_stop_cmd(const volatile uint16_t *status)
{
    assert(status != NULL);

    for (unsigned attempt = 0; attempt < STOP_ATTEMPTS; attempt++) {
        if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
            return 0;
        }

        uint32_t cmdsta =
            cc26x2_cc13x2_rfc_request_execute(RFC_CMDR_DIR_CMD(RFC_CMD_STOP));
        if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
            DEBUG("[cc26x2_cc13x2_rfc]: CMD_STOP failed, cmdsta=%08lx\n",
                  cmdsta);
            return -EIO;
        }

        for (unsigned i = 0; i < ABORT_POLL_MAX; i++) {
            if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
                return 0;
            }
        }
    }

    return -EBUSY;
}

/* RF_CPE0: every CPE interrupt except INTERNAL_ERROR, as selected in
 * RFCPEISL by cc26x2_cc13x2_rfc_request_on(). */
void isr_rfc_cpe0(void)
{
    extern void cc26x2_cc13x2_rfc_isr(void);
    cc26x2_cc13x2_rfc_isr();

    cortexm_isr_end();
}

/* RF_CPE1: INTERNAL_ERROR only. [TRM] Table 25-207: the CPE has observed an
 * unexpected error and needs a reset, done by switching the RF Core power
 * domain off and on. This handler only clears the flag. */
void isr_rfc_cpe1(void)
{
    DEBUG_PUTS("[cc26x2_cc13x2_rfc]: internal error");
    RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_INTERNAL_ERROR;
    cortexm_isr_end();
}
