/*
 * icm45686_stm_edmp.h
 *
 * Autors: Jan Rusnak.
 * (c) 2025 AZTech.
 */

/**
 * @file icm45686_stm_edmp.h
 * @brief FreeRTOS ICM-45686 eDMP/APEX tap-detection state machine using INT1.
 *
 * Owns one sensor on SPI bus 0, creates one binary ISR semaphore and one
 * acquisition task, and registers the "icms" diagnostic command. Built only
 * when ICM45686_SENSOR_MODE == ICM45686_APEX_MODE. This STM demonstrates
 * single- and double-tap detection; other APEX algorithms remain disabled.
 * There are no application event callbacks, FIFO acquisition, or runtime
 * sensor-configuration APIs in this STM.
 *
 * @par Fixed sensor and eDMP configuration
 * Following sensor soft reset, SPI pad slew setup and WHO_AM_I verification:
 * - SPI uses mode 0, 8-bit transfers, 12 MHz SCK, CSEL2 and 20 ns CS setup;
 *   CS remains asserted between transfer segments.
 * - The accelerometer runs continuously in Low-Noise mode at 400 or 800 Hz,
 *   selected by ICM45686_APEX_ODR, with UI LPF bandwidth ODR/4.
 * - The gyroscope and sensor FIFO are not used by this STM.
 * - eDMP SRAM and APEX algorithms are initialized; decimation is recomputed
 *   and APEX parameters are read, modified and written back to SRAM.
 * - Pedometer, SMD, tilt, raise-to-wake, free-fall and other APEX algorithms
 *   are disabled. Only tap detection is enabled and unmasked in APEX status.
 * - The physical INT1 source is the eDMP event signal. It is routed to INT1
 *   after the GPIO ISR has been armed and enabled, before eDMP is enabled.
 * This STM does not expose the LL's other eDMP ODRs or APEX features.
 *
 * @par Tap detection parameters
 * The six tap parameters are supplied by the client through sysconf.h:
 * - ICM45686_APEX_TAP_TMIN: single-tap analysis sub-window, 24..184
 *   samples.
 * - ICM45686_APEX_TAP_TMAX: analysis window, 49..496 samples.
 * - ICM45686_APEX_TAP_MIN_JERK: minimum candidate jerk in g/Q6, 0..64.
 * - ICM45686_APEX_TAP_SMUDGE_REJECT_THR: maximum over-threshold samples
 *   within Tmin, 13..92.
 * - ICM45686_APEX_TAP_MAX_PEAK_TOL: peak tolerance selector 1..4,
 *   corresponding to 12.5%, 25%, 37.5% or 50%.
 * - ICM45686_APEX_TAP_TAVG: axis energy window, 1, 2, 4 or 8 samples.
 * Values are written without range validation or automatic scaling when ODR
 * changes. The client must choose parameters appropriate for its selected
 * 400/800 Hz accelerometer rate; the STM logs the programmed values at start.
 * The underlying LL and device documentation define the remaining APEX
 * parameters, including their units and constraints.
 *
 * @par INT1 handling and acquisition
 * ICM45686_INT1_CONT/ICM45686_INT1_PIN come from board.h. The sensor INT1
 * output is active LOW, push-pull; GPIO input has no pull or filter and its
 * Schmitt trigger is enabled. ICM45686_INTR_TYPE selects a matched pair:
 * - FALL_EDGE_INTR_TYPE: sensor PULSE, GPIO falling-edge trigger. The captured
 *   edge is accepted even when a short pulse has already returned HIGH.
 * - LOW_LEVEL_INTR_TYPE: sensor LATCH, GPIO low-level trigger. The ISR also
 *   checks that the pin is currently LOW.
 * The armed ISR masks GPIO INT1 and signals the binary semaphore. The task
 * acknowledges INT1 status, reads the read-to-clear APEX event registers,
 * and retrieves tap data when the tap flag is present. It then re-enables
 * GPIO INT1. Non-tap/unknown events only increment diagnostic counters.
 * Each semaphore wait has a 200 ms timeout. A timeout performs a WHO_AM_I
 * check and returns to waiting; the absence of taps is normal and does not
 * trigger a no-data watchdog or reset.
 *
 * @par Tap reporting
 * The task logs SINGLE_TAP or DOUBLE_TAP with direction (+/-) and axis
 * (X/Y/Z). A double tap also logs the interval in milliseconds. The LL
 * returns double_tap_timing in accelerometer samples, in multiples of 16;
 * the task converts it using the selected 400/800 Hz sample period.
 * The direction mapping follows the TDK reference C driver: 0 is negative,
 * 1 is positive. LEDUI4 is switched OFF for single taps and ON for double
 * taps. Reporting is synchronous in the acquisition task, not in the ISR.
 * There is no text destination selector, raw sample callback or event queue.
 *
 * @par Recovery and interrupt lifetime
 * Sensor/SPI failures transition to recovery. INT1 is masked and the ISR
 * disarmed; on the next soft-reset state the binary semaphore is drained.
 * The state machine then resets and reconfigures the sensor and eDMP.
 * A GPIO callback while acquisition is disarmed cannot signal the task.
 * ICM45686_RESET_STM_TMO_MS optionally adds a client-configured recovery
 * backoff; if the macro is absent, there is no additional delay. When
 * specified, it must be positive and convert to at least one FreeRTOS tick.
 * This is a retry backoff, not a sensor reset-stabilization requirement.
 *
 * @par Build-time settings in sysconf.h and board configuration
 * - ICM45686_SENSOR_MODE: select ICM45686_APEX_MODE to build this STM.
 * - ICM45686_APEX_ODR: ICM45686_APEX_ODR_400HZ or ICM45686_APEX_ODR_800HZ.
 *   Other values are rejected at compile time.
 * - ICM45686_TASK_PRIO / ICM45686_TASK_STACK_SIZE: FreeRTOS task priority
 *   and stack depth (StackType_t units).
 * - ICM45686_INTR_TYPE: LOW_LEVEL_INTR_TYPE or FALL_EDGE_INTR_TYPE.
 * - ICM45686_PADS_SPI_SLEW: SPI output pad slew selector after reset.
 * - ICM45686_RESET_STM_TMO_MS: optional recovery backoff in milliseconds.
 * - ICM45686_APEX_TAP_*: the six tap settings listed above.
 * - ICM_45686_USE_BASIC_SMD: LL compile-time SMD variant (default 0);
 *   this STM disables SMD and does not perform SMD detection.
 * - ICM45686_CS_CONT_ID / ICM45686_CS_PIN_ID: SPI chip-select mapping;
 *   ICM45686_INT1_CONT / ICM45686_INT1_PIN: INT1 GPIO mapping in board.h.
 * REGS/FIFO report selectors and ICM45686_FIFO_HIRES have no effect on
 * this STM. Changing SPI bus, chip select, accelerometer filter, enabled
 * APEX algorithms or report destinations requires changing the implementation.
 *
 * @par Diagnostics
 * The "icms" command reports edmp_event (INT1 eDMP event status),
 * unkn_intr (INT1 wakeup without eDMP status), unkn_apex_intr (eDMP event
 * without a tap flag), irq_pin_high (accepted falling edges where the pin
 * was already HIGH), stale_irq (GPIO callbacks while disarmed), and stm_rst
 * (recovery cycles). irq_pin_high does not imply a lost tap; edmp_event is
 * not itself a count of confirmed taps.
 * The "ICM45686 STM err" counters classify fatal recovery reasons:
 * - sw_reset: sensor soft reset failure.
 * - probe: WHO_AM_I failure during initialization or a timeout ping.
 * - cfg: SPI slew, INT1 source/pin configuration or initial status failure.
 * - edmp_init: eDMP/APEX setup or activation failure.
 * - int_stat: INT1 status read failure after a semaphore wakeup.
 * - apex_stat: APEX event-status read failure.
 * - tap_data: tap result read failure.
 * Counters persist across STM recovery and reset only with firmware.
 * stm_rst is incremented after any configured backoff and can temporarily
 * lag behind the total fatal-error count.
 *
 * @see ICM-45686 datasheet DS-000577 and TDK AN-000478 eDMP/APEX guide.
 * @see icm45686-edmp.h for the lower-level APEX interface and output units.
 */

#ifndef ICM45686_STM_EDMP_H
#define ICM45686_STM_EDMP_H

/**
 * @brief Initialize the ICM-45686 eDMP tap-detection task once at startup.
 *
 * Configures the SPI device descriptor, allocates one binary semaphore,
 * registers the "icms" command and creates the FreeRTOS task. Sensor reset,
 * eDMP configuration and interrupt-driven tap detection run in that task.
 *
 * @pre SPI/GPIO/PIO backends, board pin mappings, command, message and LED
 *      services are ready. FreeRTOS task and semaphore allocation must work.
 * @note Call once during application startup from task/init context; not
 *       ISR-safe. There is no deinitialization or runtime task recreation.
 * @note Only implemented when ICM45686_SENSOR_MODE == ICM45686_APEX_MODE.
 * @note Allocation failures invoke crit_err_exit(); runtime sensor/SPI
 *       errors are handled by the STM recovery state.
 */
void init_icm45686_stm_edmp(void);

#endif
