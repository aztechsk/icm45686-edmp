/*
 * icm45686_stm_edmp.h
 *
 * Autors: Jan Rusnak.
 * (c) 2025 AZTech.
 */

/**
 * @file icm45686_stm_edmp.h
 * @brief FreeRTOS ICM-45686 eDMP/APEX TAP, SMD and Tilt state machine using INT1.
 *
 * Owns one sensor on SPI bus 0, creates one binary ISR semaphore and one
 * acquisition task, and registers the "icms" diagnostic command. Built only
 * when ICM45686_SENSOR_MODE == ICM45686_APEX_MODE. This STM implements
 * single/double-tap detection and optional SMD and Tilt events.
 * Optional text and binary event callbacks run in the acquisition task.
 * There is no FIFO acquisition or runtime sensor-configuration API.
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
 * - TAP is always enabled. SMD and Tilt are independently selectable through
 *   sysconf.h; R2W, free-fall and explicit pedometer output remain disabled.
 *   Standard SMD internally executes the pedometer even with PEDO_EN=0.
 *   Basic SMD uses WOM, configured and enabled only for that active variant.
 *   Direct WOM interrupts are not routed to INT1; APEX power save stays disabled.
 * - Enabled SMD and Tilt use client-supplied parameters; disabled algorithms
 *   retain their initialization defaults.
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
 * @par SMD and Tilt parameters
 * When SMD is enabled, the selected LL variant requires either:
 * - ICM45686_APEX_SMD_SENSITIVITY: standard SMD, 0..4 (default 0).
 *   Lower values increase detection rate and false positives.
 * - ICM45686_APEX_BASICSMD_WIN / ICM45686_APEX_BASICSMD_WIN_WAIT:
 *   Basic SMD WOM-related windows, each 25..500 samples (defaults 150/50).
 * - ICM45686_APEX_BASICSMD_WOM_X_THR / _Y_THR / _Z_THR:
 *   Required Basic SMD WOM thresholds, each 0..255 in units of 1g/256
 *   (TDK example: 6 per axis, about 23.4 mg). WOM compares consecutive
 *   samples, ORs the three axes and uses the first over-threshold event.
 *   The STM configures and enables WOM after accelerometer startup and before
 *   enabling Basic SMD, including after recovery. No host WOM interrupt is enabled.
 * When Tilt is enabled, ICM45686_APEX_TILT_WAIT_TIME is required, 0..65535
 * samples (default 200). The Tilt algorithm runs at 50 Hz for either STM ODR,
 * so each sample represents 20 ms. The 16-bit field cannot represent the
 * upper limit 65536 printed in AN-000478. All of these active settings are
 * checked at compile time, written before algorithm activation and logged.
 * Pedometer parameters retain their eDMP initialization defaults.
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
 * acknowledges INT1 status and reads both read-to-clear APEX event registers
 * in one transaction. TAP, SMD and Tilt flags are handled independently;
 * simultaneous flags can all be reported. Only TAP requires an SRAM result
 * read. The task then re-enables GPIO INT1. Unrecognized status increments
 * the corresponding diagnostic counter.
 * Each semaphore wait has a 200 ms timeout. A timeout performs a WHO_AM_I
 * check and returns to waiting; the absence of taps is normal and does not
 * trigger a no-data watchdog or reset.
 *
 * @par Event reporting
 * The task logs SINGLE_TAP or DOUBLE_TAP with direction (+/-) and axis
 * (X/Y/Z). A double tap also logs the interval in milliseconds. The LL
 * returns double_tap_timing in accelerometer samples, in multiples of 16;
 * the task converts it using the selected 400/800 Hz sample period.
 * The direction mapping follows the TDK reference C driver: 0 is negative,
 * 1 is positive. Reporting is synchronous in the acquisition task, not in
 * the ISR. Enabled SMD and Tilt events generate separate text messages.
 * ICM45686_REPORT_APEX_TAP / _SMD / _TILT select the text destination
 * independently: 0=off, 1=msg(), 2=report_fn, 3=both (default 1).
 * Callback routes require ICM45686_REPORT_FN=1. The binary event callback
 * is independent of text routing; one event per recognized algorithm flag
 * is delivered, in TAP/SMD/Tilt order, before its text report. There is no
 * internal event queue or lossless delivery guarantee. Keep callbacks bounded
 * and nonblocking to avoid delaying subsequent event handling. GPIO INT1 is
 * normally masked during event handling, but in falling-edge mode a pending
 * semaphore signal may be processed with INT1 enabled.
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
 * - ICM45686_APEX_SMD_ENABLE: 0 or 1 to disable/enable SMD.
 * - ICM45686_APEX_TILT_ENABLE: 0 or 1 to disable/enable Tilt.
 *   Both macros are required in sysconf.h. The enabled algorithms also require
 *   their applicable parameters listed above.
 * - ICM_45686_USE_BASIC_SMD: LL compile-time SMD variant, required 0 or 1;
 *   0 selects standard pedometer-based SMD, 1 selects Basic SMD.
 * - ICM45686_REPORT_APEX_TAP / _SMD / _TILT: text routing selectors, 0..3
 *   (default 1). ICM45686_REPORT_FN: compile text callback support when 1
 *   (default 0). ICM45686_REPORT_EVENT_FN: compile the binary event callback
 *   when 1 (default 0). Reporting does not change the enabled algorithms.
 * - ICM45686_CS_CONT_ID / ICM45686_CS_PIN_ID: SPI chip-select mapping;
 *   ICM45686_INT1_CONT / ICM45686_INT1_PIN: INT1 GPIO mapping in board.h.
 * REGS/FIFO measurement selectors, ICM45686_REPORT_SAMPLE_FN,
 * ICM45686_REPORT_MSG_NUM and ICM45686_FIFO_HIRES have no effect on this STM.
 * Changing SPI bus, chip select, accelerometer filter or additional APEX
 * algorithms requires changing the implementation.
 *
 * @par Diagnostics
 * The "icms" command reports edmp_event (INT1 eDMP event status),
 * apex_event (all recognized algorithm flags, also the binary event sequence),
 * smd_event and tilt_event (recognized algorithm status flags),
 * unkn_intr (INT1 wakeup without eDMP status), unkn_apex_intr (eDMP event
 * without any TAP/SMD/Tilt flag), irq_pin_high (accepted edges where the pin
 * was already HIGH), stale_irq (GPIO callbacks while disarmed), and stm_rst
 * (recovery cycles). irq_pin_high does not imply a lost tap; edmp_event is
 * not itself a count of confirmed taps. SMD/Tilt enable changes do not
 * modify the tap detection parameters or its classification latency.
 * The "ICM45686 STM err" counters classify fatal recovery reasons:
 * - sw_reset: sensor soft reset failure.
 * - probe: WHO_AM_I failure during initialization or a timeout ping.
 * - cfg: SPI slew, INT1 source/pin configuration or initial status failure.
 * - edmp_init: eDMP/APEX setup or activation failure.
 * - int_stat: INT1 status read failure after a semaphore wakeup.
 * - apex_stat: APEX event-status read failure.
 * - tap_data: tap result read failure.
 * When enabled, report_fn_err and event_fn_err count callbacks returning
 * FALSE. An absent callback is not an error; callback failures do not
 * trigger recovery. Counters persist across STM recovery and reset only
 * with firmware.
 * stm_rst is incremented after any configured backoff and can temporarily
 * lag behind the total fatal-error count.
 *
 * @see ICM-45686 datasheet DS-000577 and TDK AN-000478 eDMP/APEX guide.
 * @see icm45686-edmp.h for the lower-level APEX interface and output units.
 */

#ifndef ICM45686_STM_EDMP_H
#define ICM45686_STM_EDMP_H

#ifndef ICM45686_REPORT_FN
#define ICM45686_REPORT_FN 0
#endif
#ifndef ICM45686_REPORT_EVENT_FN
#define ICM45686_REPORT_EVENT_FN 0
#endif
#if ICM45686_REPORT_FN != 0 && ICM45686_REPORT_FN != 1
#error "ICM45686_REPORT_FN must be 0 or 1"
#endif
#if ICM45686_REPORT_EVENT_FN != 0 && ICM45686_REPORT_EVENT_FN != 1
#error "ICM45686_REPORT_EVENT_FN must be 0 or 1"
#endif

#if ICM45686_REPORT_FN == 1 || ICM45686_REPORT_EVENT_FN == 1
#include <gentyp.h>
#endif

/** @brief Recognized APEX event type codes. */
enum icm45686_apex_event_type {
	ICM45686_APEX_EVENT_SINGLE_TAP = 1,
	ICM45686_APEX_EVENT_DOUBLE_TAP = 2,
	ICM45686_APEX_EVENT_SMD = 3,
	ICM45686_APEX_EVENT_TILT = 4
};

#if ICM45686_REPORT_EVENT_FN == 1
#include <stdint.h>

/**
 * @brief One recognized APEX event delivered to the binary callback.
 *
 * The fixed-width fields are host values, not a packed wire format. Tap axis
 * and direction retain their LL encodings and are valid only for TAP events.
 * double_tap_ms is the interval in milliseconds for DOUBLE_TAP, otherwise 0.
 * The sequence counts recognized events independently of delivery, starts at
 * 1, wraps modulo 2^32 and persists across sensor/STM recovery. It does not
 * count events lost in the sensor or GPIO path.
 */
struct icm45686_apex_event {
	uint32_t sequence; /**< Recognized event sequence. */
	uint16_t double_tap_ms; /**< Double-tap interval in ms; otherwise 0. */
	uint8_t type; /**< enum icm45686_apex_event_type code. */
	uint8_t tap_axis; /**< X=0, Y=1, Z=2 for TAP; otherwise 0. */
	uint8_t tap_direction; /**< Negative=0, positive=1 for TAP; otherwise 0. */
};
#endif

/**
 * @brief Initialize the ICM-45686 eDMP/APEX event task once at startup.
 *
 * Configures the SPI device descriptor, allocates one binary semaphore,
 * registers the "icms" command and creates the FreeRTOS task. Sensor reset,
 * eDMP configuration and interrupt-driven APEX detection run in that task.
 *
 * @pre SPI/GPIO/PIO backends, board pin mappings, command and message
 *      services are ready. FreeRTOS task and semaphore allocation must work.
 * @note Call once during application startup from task/init context; not
 *       ISR-safe. There is no deinitialization or runtime task recreation.
 * @note Only implemented when ICM45686_SENSOR_MODE == ICM45686_APEX_MODE.
 * @note Allocation failures invoke crit_err_exit(); runtime sensor/SPI
 *       errors are handled by the STM recovery state.
 */
void init_icm45686_stm_edmp(void);

#if ICM45686_REPORT_FN == 1
/**
 * @brief Install or clear the formatted event-text callback.
 *
 * Invoked once for each event routed to it (selector 2 or 3), with the
 * same NUL-terminated text and trailing newline as local logging.
 * Returning FALSE increments report_fn_err_cnt without triggering recovery.
 *
 * @param[in] report_fn Callback function, or NULL to disable it.
 * @note Runs synchronously in the acquisition task, not an ISR. Copy the
 *       text if retaining it; the buffer is reused. Keep callbacks bounded
 *       and nonblocking. Register during initialization, with sysconf.h
 *       included before this header; concurrent replacement is not synchronized.
 */
void icm45686_stm_set_report_fn(boolean_t (*report_fn)(const char *));
#endif

#if ICM45686_REPORT_EVENT_FN == 1
/**
 * @brief Install or clear the binary APEX event callback.
 *
 * Called for each recognized TAP, SMD or Tilt event independently of text
 * routing, after successful status/result reads. Returning FALSE increments
 * event_fn_err_cnt without triggering recovery.
 *
 * @param[in] event_fn Callback function, or NULL to disable it.
 * @note Runs synchronously in the acquisition task, not an ISR. The event
 *       pointer is valid only during the call; copy the fields before return
 *       if retaining them. Keep callbacks bounded and nonblocking. Register
 *       during initialization; concurrent replacement is not synchronized.
 */
void icm45686_stm_set_event_fn(boolean_t (*event_fn)(const struct icm45686_apex_event *));
#endif

#endif
