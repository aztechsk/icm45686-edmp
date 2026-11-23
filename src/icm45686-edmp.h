/*
 * icm45686-edmp.h
 *
 * Copyright (c) 2024 InvenSense, Inc.
 * Copyright (c) 2025 Jan Rusnak <jan@rusnak.sk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/** @defgroup EDMP EDMP
 *  @brief API to drive eDMP features.
 *  @{
 *
 *  @note All functions return 0 on success.
 *        Negative values indicate errors:
 *        - `-ENRDY`, `-EHW`, `-EDATA`, `-ETMO`
 *        - `-EHW`, `-EDMA` propagated from the ICM-45686 (icm45686.h) driver.
 *  @note Calls use synchronous SPI/IREG operations and must not run in an ISR.
 *        The caller must serialize access to the same device; there is no driver lock.
 *  @note Algorithm enable functions only update feature bits. Accelerometer mode,
 *        eDMP enable and physical interrupt routing are configured separately.
 */

/** @file icm45686-edmp.h */

#ifndef ICM45686_EDMP_H
#define ICM45686_EDMP_H

#ifndef ICM_45686_USE_BASIC_SMD
#error "ICM_45686_USE_BASIC_SMD not defined; define it in sysconf.h"
#endif
#if ICM_45686_USE_BASIC_SMD != 0 && ICM_45686_USE_BASIC_SMD != 1
#error "ICM_45686_USE_BASIC_SMD must be 0 or 1"
#endif

#include <stdint.h>
#include <string.h>
#include "icm45686.h"

/** @brief Write a fixed-size parameter to eDMP SRAM using IREG.
 *  @param[in] s     Initialized device handle.
 *  @param[in] name  Parameter address symbol with a matching name##_SIZE.
 *  @param[in] val   Pointer to at least name##_SIZE bytes; not modified.
 *  @return          0 on success, negative IREG/SPI error otherwise.
 */
#define ICM_45686_WRITE_EDMP_SRAM(s, name, val) icm_45686_ireg_write(s, (uint32_t) name, val, name##_SIZE)

/** @brief Read a fixed-size parameter from eDMP SRAM using IREG.
 *  @param[in] s     Initialized device handle.
 *  @param[in] name  Parameter address symbol with a matching name##_SIZE.
 *  @param[out] val  Writable buffer of at least name##_SIZE bytes.
 *  @return          0 on success, negative IREG/SPI error otherwise.
 */
#define ICM_45686_READ_EDMP_SRAM(s, name, val) icm_45686_ireg_read(s, (uint32_t) name, val, name##_SIZE)

/** @brief eDMP internal interrupt inputs (IRQ0-IRQ2), not physical INT1/INT2 pins. */
typedef enum { ICM_45686_EDMP_INT0 = 0, ICM_45686_EDMP_INT1, ICM_45686_EDMP_INT2 } icm_45686_edmp_int_t;

/** @brief APEX interrupt masks and status flags.
 *  @note For configuration, 1 requests an unmasked event and 0 masks it.
 *        For status, 1 indicates a reported event. The b2s and b2s_rev
 *        members are not populated or configured by the current driver.
 */
typedef struct {
	uint8_t tap;
	uint8_t highg;
	uint8_t lowg;
	uint8_t tilt_det;
	uint8_t step_cnt_ovfl;
	uint8_t step_det;
	uint8_t ff;
	uint8_t r2w;
	uint8_t b2s;
	uint8_t r2w_sleep;
	uint8_t b2s_rev;
	uint8_t smd;
	uint8_t self_test;
	uint8_t sec_auth;
} icm_45686_edmp_int_state_t;

/** Registers to retrieve interrupts status for APEX. */
typedef struct {
	struct icm_45686_int_apex_status0 int_apex_status0;
	struct icm_45686_int_apex_status1 int_apex_status1;
} icm_45686_int_apex_statusx_t;

/** Registers to configure interrupts for APEX. */
typedef struct {
	struct icm_45686_int_apex_config0 int_apex_config0;
	struct icm_45686_int_apex_config1 int_apex_config1;
} icm_45686_int_apex_configx_t;

/** Registers to enable APEX features. */
typedef struct {
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;
} icm_45686_edmp_apex_enx_t;

/** @brief APEX algorithm configuration values stored in eDMP SRAM.
 *  @note Parameter ranges and defaults are described in AN-000478, section 4.2.3,
 *        and in icm45686-edmp_def.h. The driver does not validate those ranges.
 *  @note Pedometer amplitude thresholds are in g/Q25; tilt and step timers count
 *        algorithm samples. R2W timeouts are in ms and its angle cosine uses Q30.
 *        LowG/HighG thresholds are in g/Q12; freefall and tap timers count samples.
 *        Tap jerk is in g/Q6, the soft-iron matrix uses Q14, and power_save_time
 *        counts samples. power_save_en is the EDMP_APEX_EN1 register flag.
 */
typedef struct {
	/* Pedometer */
	uint32_t ped_amp_th;
	uint16_t ped_step_cnt_th;
	uint16_t ped_step_det_th;
	uint16_t ped_sb_timer_th;
	uint32_t ped_hi_en_th;
	uint8_t  ped_sensitivity_mode;
	uint32_t ped_low_en_amp_th;
	/* Tilt */
	uint16_t tilt_wait_time;
#if ICM_45686_USE_BASIC_SMD == 1
	/* Basic SMD */
	uint32_t basicsmd_win;
	uint32_t basicsmd_win_wait;
#else
	/* SMD */
	uint8_t smd_sensitivity;
#endif
	/* R2W */
	uint32_t r2w_sleep_time_out;
	uint32_t r2w_sleep_gesture_delay;
	uint32_t r2w_mounting_matrix;
	uint32_t r2w_gravity_filter_gain;
	uint32_t r2w_motion_th_angle_cosine;
	uint32_t r2w_motion_th_timer_fast;
	uint32_t r2w_motion_th_timer_slow;
	uint32_t r2w_motion_prev_gravity_timeout;
	uint32_t r2w_last_gravity_motion_timer;
	uint32_t r2w_last_gravity_timeout;
	uint32_t r2w_gesture_validity_timeout;
	/* Freefall */
	uint16_t lowg_peak_th;
	uint16_t lowg_peak_th_hyst;
	uint16_t lowg_time_th;
	uint16_t highg_peak_th;
	uint16_t highg_peak_th_hyst;
	uint16_t highg_time_th;
	uint32_t ff_min_duration;
	uint32_t ff_max_duration;
	uint32_t ff_debounce_duration;
	/* Tap */
	uint8_t  tap_min_jerk;
	uint16_t tap_tmax;
	uint8_t  tap_tmin;
	uint8_t  tap_max_peak_tol;
	uint8_t  tap_smudge_reject_th;
	uint8_t  tap_tavg;
	/* CalMag */
	int32_t soft_iron_sensitivity_matrix[9];
	int32_t hard_iron_offset[3];
	/* Power save */
	uint32_t power_save_time;
	uint8_t  power_save_en;
} icm_45686_edmp_apex_parameters_t;

/** @brief Pedometer activity class */
typedef enum {
	ICM_45686_EDMP_UNKNOWN = 0,
	ICM_45686_EDMP_WALK = 1,
	ICM_45686_EDMP_RUN = 2,
} icm_45686_edmp_activity_class_t;

/** @brief Pedometer outputs */
typedef struct {
	/** @brief Number of steps. */
	uint16_t step_cnt;
	/** @brief Encoded step interval (unsigned Q6.2, four units per sample).
	 *         For a nonzero value, cadence (steps/s) = 4 * pedometer_odr_hz / step_cadence.
	 *         Pedometer ODR is 50 Hz for eDMP ODR 50-800 Hz, and 25 Hz for eDMP ODR 25 Hz.
	 */
	uint8_t step_cadence;
	/** @brief Detected activity. */
	icm_45686_edmp_activity_class_t activity_class;
} icm_45686_edmp_pedometer_data_t;

/** @brief Tap number definition */
typedef enum {
	ICM_45686_EDMP_TAP_DOUBLE = 0x02,
	ICM_45686_EDMP_TAP_SINGLE = 0x01,
} icm_45686_edmp_tap_num_t;

/** @brief Tap axis definition */
typedef enum {
	ICM_45686_EDMP_TAP_AXIS_Z = 0x02,
	ICM_45686_EDMP_TAP_AXIS_Y = 0x01,
	ICM_45686_EDMP_TAP_AXIS_X = 0x00,
} icm_45686_edmp_tap_axis_t;

/** @brief Tap direction encoded by eDMP.
 *  @note 0 = negative, 1 = positive, matching the TDK reference C driver.
 */
typedef enum {
	ICM_45686_EDMP_TAP_DIR_POSITIVE = 0x01,
	ICM_45686_EDMP_TAP_DIR_NEGATIVE = 0x00,
} icm_45686_edmp_tap_dir_t;

/** @brief Tap event details read from eDMP SRAM.
 *  @note For double taps, double_tap_timing is the interval in accelerometer
 *        samples (16 * raw DOUBLE_TAP_TIMING). It is zero for other events.
 */
typedef struct {
	icm_45686_edmp_tap_num_t num;
	icm_45686_edmp_tap_axis_t axis;
	icm_45686_edmp_tap_dir_t direction;
	uint16_t double_tap_timing;
} icm_45686_edmp_tap_data_t;

/** @brief Select the eDMP processing output data rate.
 *  @warning Accelerometer ODR must be greater than or equal to eDMP ODR.
 *           Accelerometer LP mode is supported at ODR <= 400 Hz; above that use LN mode.
 *  @warning If icm_45686_edmp_init_apex() was already called, application should call
 *          `icm_45686_edmp_recompute_apex_decimation()` afterwards if APEX algorithms are to be run.
 *  @param[in] s         Pointer to device.
 *  @param[in] frequency One of 25, 50, 100, 200, 400 or 800 Hz (APEX ODR enum).
 *  @note Invalid @p frequency invokes crit_err_exit(BAD_PARAMETER).
 *  @return              0 on success, negative value on error.
 */
int icm_45686_edmp_set_frequency(icm_45686 s, enum icm_45686_dmp_ext_sen_odr_cfg_apex_odr frequency);

/** @brief Initialize the eDMP ROM algorithms and their SRAM parameters.
 *  @pre eDMP must be disabled.
 *  @note Select the desired eDMP ODR before calling. Initialize after sensor
 *        reset and before configuring/enabling APEX features.
 *        Initialization powers up SRAM, clears the algorithm SRAM region and
 *        restores the APEX algorithm defaults. Existing SRAM values are lost.
 *  @warning This function will reset all interrupt masks previously set with
 *           `icm_45686_edmp_unmask_int_src()` and exit with `ICM_45686_EDMP_INT_SRC_ACCEL_DRDY_MASK` unmasked on
 *           `ICM_45686_EDMP_INT0`.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error or if EDMP is enabled.
 */
int icm_45686_edmp_init_apex(icm_45686 s);

/** @brief Reinitialize APEX decimation after changing eDMP ODR.
 *
 *  Re-runs the eDMP initialization procedure and restores the feature enable
 *  register values, but resets algorithm parameters to their defaults.
 *  @warning It is up to application level to save/restore previously configured APEX parameters,
 *           if any, with `icm_45686_edmp_set_apex_parameters()`.
 *  @warning EDMP must be disabled before calling this function.
 *  @warning This function will reset all interrupt masks previously set with
 *           `icm_45686_edmp_unmask_int_src()` and exit with `ICM_45686_EDMP_INT_SRC_ACCEL_DRDY_MASK` unmasked on
 *           `ICM_45686_EDMP_INT0`.
 *  @param[in] s         Pointer to device.
 *  @return              0 on success, negative value on error or if EDMP is enabled.
 */
int icm_45686_edmp_recompute_apex_decimation(icm_45686 s);

/** @brief Read current APEX parameter values from SRAM and power-save configuration.
 *  @param[in] s   Initialized device handle.
 *  @param[out] p  Caller-owned parameter structure; may be partially updated on failure.
 *  @return        0 on success, negative IREG/SPI error otherwise.
 */
int icm_45686_edmp_get_apex_parameters(icm_45686 s, icm_45686_edmp_apex_parameters_t *p);

/** @brief Write APEX parameters to SRAM and update the power-save enable flag.
 *  @pre Disable all APEX algorithms before calling. Supported values and units
 *       are defined by the eDMP parameter specifications; they are not validated here.
 *  @note On an I/O error, earlier parameter writes are not rolled back.
 *  @note For APEX power saving, TDK specifies WOM_EN and a nonzero power_save_time.
 *  @param[in] s  Initialized device handle.
 *  @param[in] p  Caller-owned, read-only parameter structure.
 *  @return       0 on success, -ENRDY if an algorithm is enabled, or a negative I/O error.
 */
int icm_45686_edmp_set_apex_parameters(icm_45686 s, const icm_45686_edmp_apex_parameters_t *p);

/** @brief Read the APEX event interrupt mask configuration.
 *  @note A returned 1 means unmasked and 0 means masked. b2s/b2s_rev are
 *        not written by this function; initialize the structure if using them.
 *  @param[in] s    Initialized device handle.
 *  @param[out] it  Caller-owned interrupt configuration structure.
 *  @return         0 on success, negative I/O error otherwise.
 */
int icm_45686_edmp_get_config_int_apex(icm_45686 s, icm_45686_edmp_int_state_t *it);

/** @brief Configure the APEX event interrupt masks.
 *  @note A nonzero member unmasks its event; zero masks it. The compile-time
 *        ICM_45686_USE_BASIC_SMD selection determines which SMD mask is used.
 *        b2s and b2s_rev are ignored. This does not route events to INT1/INT2.
 *  @param[in] s   Initialized device handle.
 *  @param[in] it  Caller-owned interrupt configuration structure.
 *  @return        0 on success, negative I/O error otherwise.
 */
int icm_45686_edmp_set_config_int_apex(icm_45686 s, const icm_45686_edmp_int_state_t *it);

/** @brief  Enable EDMP.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_enable(icm_45686 s);

/** @brief  Disable EDMP.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable(icm_45686 s);

/** @brief  Enable APEX algorithm Pedometer.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_pedometer(icm_45686 s);

/** @brief  Disable APEX algorithm Pedometer.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_pedometer(icm_45686 s);

/** @brief  Enable APEX algorithm Significant Motion Detection.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_smd(icm_45686 s);

/** @brief  Disable APEX algorithm Significant Motion Detection.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_smd(icm_45686 s);

/** @brief  Enable APEX algorithm Tilt.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_tilt(icm_45686 s);

/** @brief  Disable APEX algorithm Tilt.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_tilt(icm_45686 s);

/** @brief  Enable APEX algorithm R2W.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_r2w(icm_45686 s);

/** @brief  Disable APEX algorithm R2W.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_r2w(icm_45686 s);

/** @brief Enable the APEX tap-detection algorithm.
 *  @warning Tap detection is supported only at eDMP ODR 200, 400 or 800 Hz
 *           (AN-000478, section 4.3.1.1).
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_tap(icm_45686 s);

/** @brief  Disable APEX algorithm Tap.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_tap(icm_45686 s);

/** @brief  Enable APEX algorithm Free Fall.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success
 *                -ENRDY if user should have called `icm_45686_edmp_recompute_apex_decimation()`,
 *                other negative value on error.
 */
int icm_45686_edmp_enable_ff(icm_45686 s);

/** @brief  Disable APEX algorithm Free Fall.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_disable_ff(icm_45686 s);

/** @brief Read and clear the two APEX event status registers in one burst.
 *  @note INT_APEX_STATUS0/1 are read-to-clear registers. b2s/b2s_rev are
 *        not written by this function; initialize the structure if using them.
 *  @param[in] s    Initialized device handle.
 *  @param[out] it  Caller-owned event-status structure.
 *  @return         0 on success, negative I/O error otherwise.
 */
int icm_45686_edmp_get_int_apex_status(icm_45686 s, icm_45686_edmp_int_state_t *it);

/** @brief Read pedometer count, encoded step interval and activity class.
 *  @note The step count is read repeatedly until two consecutive readings agree.
 *  @param[in] s      Initialized device handle.
 *  @param[out] data  Caller-owned output structure.
 *  @return           0 on success, -EHW if stable step count cannot be read,
 *                    or another negative I/O error.
 */
int icm_45686_edmp_get_pedometer_data(icm_45686 s, icm_45686_edmp_pedometer_data_t *data);

/** @brief Read and consume one buffered freefall duration.
 *  @note The duration is in accelerometer samples; divide by accel ODR in Hz
 *        to obtain seconds. Successful reads advance the buffer read pointer.
 *  @param[in] s                   Initialized device handle.
 *  @param[out] freefall_duration  Caller-owned output, in accelerometer samples.
 *  @return                        0 on success, -EDATA if no result is buffered,
 *                                 or another negative I/O error.
 */
int icm_45686_edmp_get_ff_data(icm_45686 s, uint16_t *freefall_duration);

/** @brief Read last tap type, axis, direction and double-tap timing.
 *  @note For double taps, double_tap_timing is in accelerometer samples,
 *        quantized in steps of 16. Divide by accelerometer ODR (Hz) for
 *        seconds. The timing is zero for other tap events.
 *  @param[in] s      Initialized device handle.
 *  @param[out] data  Caller-owned tap output structure.
 *  @return           0 on success, negative I/O error otherwise.
 */
int icm_45686_edmp_get_tap_data(icm_45686 s, icm_45686_edmp_tap_data_t *data);

/** @brief Mask selected internal eDMP interrupt input sources.
 *  @note Invalid @p edmp_int_nb invokes crit_err_exit(BAD_PARAMETER).
 *  @param[in] s            Initialized device handle.
 *  @param[in] edmp_int_nb  Internal eDMP IRQ0, IRQ1 or IRQ2.
 *  @param[in] int_mask     Source bitmask; set bits are masked.
 *  @return                 0 on success, negative value on error.
 */
int icm_45686_edmp_mask_int_src(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb, uint8_t int_mask);

/** @brief Unmask selected internal eDMP interrupt input sources.
 *  @note Invalid @p edmp_int_nb invokes crit_err_exit(BAD_PARAMETER).
 *  @param[in] s            Initialized device handle.
 *  @param[in] edmp_int_nb  Internal eDMP IRQ0, IRQ1 or IRQ2.
 *  @param[in] int_mask     Source bitmask; set bits are unmasked.
 *  @return                 0 on success, negative value on error.
 */
int icm_45686_edmp_unmask_int_src(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb, uint8_t int_mask);

/** @brief Program eDMP ROM entry points and stack start address.
 *  @note This prepares execution but does not start eDMP.
 *  @param[in] s  Pointer to device.
 *  @return       0 on success, negative value on error.
 */
int icm_45686_edmp_configure(icm_45686 s);

/** @brief Trigger eDMP execution through an internal on-demand IRQ source.
 *  @note Invalid @p edmp_int_nb invokes crit_err_exit(BAD_PARAMETER).
 *  @note Unmasks the on-demand source and enables eDMP. After the trigger,
 *        waits 200 us for eDMP startup, not for execution completion. Use
 *        icm_45686_edmp_wait_for_idle() to wait for completion.
 *        The source remains unmasked after return.
 *  @param[in] s            Initialized device handle.
 *  @param[in] edmp_int_nb  Internal eDMP IRQ0, IRQ1 or IRQ2.
 *  @return                 0 on successful trigger, negative I/O error otherwise.
 */
int icm_45686_edmp_run_ondemand(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb);

/** @brief Poll EDMP_IDLE until the eDMP finishes execution.
 *  @note Blocking IREG polling with a nominal 1 s timeout accumulated from
 *        5 us delays; actual wall time also includes IREG transactions.
 *  @param[in] s  Initialized device handle.
 *  @return       0 when idle, -ETMO after timeout, or another negative I/O error.
 */
int icm_45686_edmp_wait_for_idle(icm_45686 s);

#endif

/** @} */
