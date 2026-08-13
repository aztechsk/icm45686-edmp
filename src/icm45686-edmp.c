/*
 * icm45686-edmp.c
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

#include <gentyp.h>
#include <stdint.h>
#include "sysconf.h"
#include "dlycnt.h"
#include "hwerr.h"
#include "icm45686-edmp_def.h"
#include "icm45686-edmp.h"

#define EDMP_ROM_START_ADDR_IRQ0 EDMP_ROM_BASE
#define EDMP_ROM_START_ADDR_IRQ1 (EDMP_ROM_BASE + 0x04)
#define EDMP_ROM_START_ADDR_IRQ2 (EDMP_ROM_BASE + 0x08)

static int check_dmp_odr_decimation(icm_45686 s);

/**
 * icm_45686_edmp_set_frequency
 */
int icm_45686_edmp_set_frequency(icm_45686 s, enum icm_45686_dmp_ext_sen_odr_cfg_apex_odr frequency)
{
	int status;
	struct icm_45686_dmp_ext_sen_odr_cfg dmp_ext_sen_odr_cfg;

	if ((status = icm_45686_bank0_read(s, ICM_45686_DMP_EXT_SEN_ODR_CFG, &dmp_ext_sen_odr_cfg, 1))) {
		return (status);
	}
	dmp_ext_sen_odr_cfg.apex_odr = frequency;
	return (icm_45686_bank0_write(s, ICM_45686_DMP_EXT_SEN_ODR_CFG, &dmp_ext_sen_odr_cfg, 1, TRUE));
}

/**
 * icm_45686_edmp_init_apex
 */
int icm_45686_edmp_init_apex(icm_45686 s)
{
	int status;
	struct icm_45686_apex_buffer_mgmt apex_buffer_mgmt;
	struct icm_45686_fifo_sram_sleep fifo_sram_sleep;
	uint8_t value;

	/* Configure DMP address registers */
	if ((status = icm_45686_edmp_configure(s))) {
		return (status);
	}
	/*
	 * Initialize read and write pointers for pedometer to buffer full condition. EDMP will always
	 * write step count in EDMP_PED_STEP_CNT_BUF2.
	 * Initialize read and write pointers for freefall to 0.
	 */
	if ((status = icm_45686_bank0_read(s, ICM_45686_APEX_BUFFER_MGMT, &apex_buffer_mgmt, 1))) {
		return (status);
	}
	apex_buffer_mgmt.step_count_host_rptr = 2;
	apex_buffer_mgmt.step_count_edmp_wptr = 0;
	apex_buffer_mgmt.ff_duration_host_rptr = 0;
	apex_buffer_mgmt.ff_duration_edmp_wptr = 0;
	if ((status = icm_45686_bank0_write(s, ICM_45686_APEX_BUFFER_MGMT, &apex_buffer_mgmt, 1, TRUE))) {
		return (status);
	}
	/* Same impl as adv_power_up_sram, duplicated here to prevent dependency */
	if ((status = icm_45686_ireg_read(s, ICM_45686_FIFO_SRAM_SLEEP, &fifo_sram_sleep, 1))) {
		return (status);
	}
	fifo_sram_sleep.fifo_gsleep_shared_sram = 0x03;
	if ((status = icm_45686_ireg_write(s, ICM_45686_FIFO_SRAM_SLEEP, &fifo_sram_sleep, 1))) {
		return (status);
	}
	/* Clear SRAM */
	value = 0;
	for (int i = 0; i < EDMP_ROM_DATA_SIZE; i++) {
		if ((status = icm_45686_ireg_write(s, (uint32_t) EDMP_RAM_BASE + i, &value, 1))) {
			return (status);
		}
	}
	return (icm_45686_edmp_recompute_apex_decimation(s));
}

/**
 * icm_45686_edmp_recompute_apex_decimation
 */
int icm_45686_edmp_recompute_apex_decimation(icm_45686 s)
{
	int status;
	uint8_t value;
	struct icm_45686_edmp_apex_en0 save_edmp_apex_en0;
	struct icm_45686_edmp_apex_en1 save_edmp_apex_en1;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0 = {0};
	struct icm_45686_edmp_apex_en1 edmp_apex_en1 = {0};
	struct icm_45686_reg_host_msg reg_host_msg;

	/*
	 * Check that DMP is turned OFF before requesting init APEX and save DMP enabled bits before
	 * requesting init procedure
	 */
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &save_edmp_apex_en0, 1))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &save_edmp_apex_en1, 1))) {
		return (status);
	}
	if (save_edmp_apex_en1.edmp_enable) {
		return (-ENRDY);
	}
	/*
	 * Make sure that all DMP interrupts are masked by default, to not trigger unexpected algorithm
	 *  execution when initialization is done if any sensor is running
	 */
	value = 0x3F;
	if ((status = icm_45686_ireg_write(s, ICM_45686_STATUS_MASK_PIN_0_7, &value, 1))) {
		return (status);
	}
	if ((status = icm_45686_ireg_write(s, ICM_45686_STATUS_MASK_PIN_8_15, &value, 1))) {
		return (status);
	}
	if ((status = icm_45686_ireg_write(s, ICM_45686_STATUS_MASK_PIN_16_23, &value, 1))) {
		return (status);
	}
	/* Trigger EDMP with on-demand mode */
	if ((status = icm_45686_edmp_unmask_int_src(s, ICM_45686_EDMP_INT0, ICM_45686_EDMP_INT_SRC_ON_DEMAND_MASK))) {
		return (status);
	}
	/*
	 * Request to execute init procedure, make sure init is the only feature enabled
	 * (overwrite previously saved config)
	 */
	if ((status = icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE))) {
		return (status);
	}
	edmp_apex_en1.init_en = ICM_45686_ENABLE;
	edmp_apex_en1.edmp_enable = ICM_45686_ENABLE;
	if ((status = icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1, TRUE))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_REG_HOST_MSG, &reg_host_msg, 1))) {
		return (status);
	}
	reg_host_msg.edmp_on_demand_en = ICM_45686_ENABLE;
	if ((status = icm_45686_bank0_write(s, ICM_45686_REG_HOST_MSG, &reg_host_msg, 1, TRUE))) {
		return (status);
	}
	/* Wait 200 us to give enough time for EMDP to start running */
	delay_us(200);
	/* Wait for DMP execution to complete */
	if ((status = icm_45686_edmp_wait_for_idle(s))) {
		return (status);
	}
	/* Reset states */
	if ((status = icm_45686_edmp_mask_int_src(s, ICM_45686_EDMP_INT0, ICM_45686_EDMP_INT_SRC_ON_DEMAND_MASK))) {
		return (status);
	}
	/* Restore original DMP state, with DMP necessarily disabled as it was checked at the beginning of this function */
	if ((status = icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &save_edmp_apex_en0, 1, TRUE))) {
		return (status);
	}
	if ((status = icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &save_edmp_apex_en1, 1, TRUE))) {
		return (status);
	}
	return (icm_45686_edmp_unmask_int_src(s, ICM_45686_EDMP_INT0, ICM_45686_EDMP_INT_SRC_ACCEL_DRDY_MASK));
}

/**
 * icm_45686_edmp_get_apex_parameters
 */
int icm_45686_edmp_get_apex_parameters(icm_45686 s, icm_45686_edmp_apex_parameters_t *p)
{
	int status;
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;

	/* Pedometer */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_AMP_TH, &p->ped_amp_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_STEP_CNT_TH, &p->ped_step_cnt_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_STEP_DET_TH, &p->ped_step_det_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_SB_TIMER_TH, &p->ped_sb_timer_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_HI_EN_TH, &p->ped_hi_en_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_SENSITIVITY_MODE, &p->ped_sensitivity_mode))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_LOW_EN_AMP_TH, &p->ped_low_en_amp_th))) {
		return (status);
	}
	/* Tilt */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TILT_WAIT_TIME, &p->tilt_wait_time))) {
		return (status);
	}
#if ICM_45686_USE_BASIC_SMD == 1
	/* Basic SMD */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_BASICSMD_WIN, &p->basicsmd_win))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_BASICSMD_WIN_WAIT, &p->basicsmd_win_wait))) {
		return (status);
	}
#else
	/* SMD */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_SMD_SENSITIVITY, &p->smd_sensitivity))) {
		return (status);
	}
#endif
	/* R2W */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_SLEEP_TIME_OUT, &p->r2w_sleep_time_out))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_SLEEP_GESTURE_DELAY, &p->r2w_sleep_gesture_delay))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_MOUNTING_MATRIX, &p->r2w_mounting_matrix))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_GRAVITY_FILTER_GAIN, &p->r2w_gravity_filter_gain))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_ANGLE_COSINE, &p->r2w_motion_th_angle_cosine))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_TIMER_FAST, &p->r2w_motion_th_timer_fast))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_TIMER_SLOW, &p->r2w_motion_th_timer_slow))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_MOTION_PREV_GRAVITY_TIMEOUT, &p->r2w_motion_prev_gravity_timeout))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_LAST_GRAVITY_MOTION_TIMER, &p->r2w_last_gravity_motion_timer))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_LAST_GRAVITY_TIMEOUT, &p->r2w_last_gravity_timeout))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_R2W_GESTURE_VALIDITY_TIMEOUT, &p->r2w_gesture_validity_timeout))) {
		return (status);
	}
	/* Freefall */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_LOWG_PEAK_TH, &p->lowg_peak_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_LOWG_PEAK_TH_HYST, &p->lowg_peak_th_hyst))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_LOWG_TIME_TH, &p->lowg_time_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_HIGHG_PEAK_TH, &p->highg_peak_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_HIGHG_PEAK_TH_HYST, &p->highg_peak_th_hyst))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_HIGHG_TIME_TH, &p->highg_time_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_FF_MIN_DURATION, &p->ff_min_duration))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_FF_MAX_DURATION, &p->ff_max_duration))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_FF_DEBOUNCE_DURATION, &p->ff_debounce_duration))) {
		return (status);
	}
	/* Tap */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_MIN_JERK, &p->tap_min_jerk))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_TMAX, &p->tap_tmax))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_TMIN, &p->tap_tmin))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_MAX_PEAK_TOL, &p->tap_max_peak_tol))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_SMUDGE_REJECT_THR, &p->tap_smudge_reject_th))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_TAVG, &p->tap_tavg))) {
		return (status);
	}
	/* CalMag */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_SOFT_IRON_SENSITIVITY_MATRIX, p->soft_iron_sensitivity_matrix))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_HARD_IRON_OFFSET, p->hard_iron_offset))) {
		return (status);
	}
	/* Power save */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_POWER_SAVE_TIME, &p->power_save_time))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1))) {
		return (status);
	}
	p->power_save_en = edmp_apex_en1.power_save_en ? ICM_45686_ENABLE : ICM_45686_DISABLE;
	return (0);
}

/**
 * icm_45686_edmp_set_apex_parameters
 */
int icm_45686_edmp_set_apex_parameters(icm_45686 s, icm_45686_edmp_apex_parameters_t *p)
{
	int status;
	icm_45686_edmp_apex_enx_t cfg;

	/* DMP cannot be configured if it is running, hence make sure all APEX algorithms are off */
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &cfg, 2))) {
		return (status);
	}
	if (cfg.edmp_apex_en0.pedo_en || cfg.edmp_apex_en0.tilt_en || cfg.edmp_apex_en0.ff_en ||
	    cfg.edmp_apex_en0.smd_en || cfg.edmp_apex_en0.tap_en || cfg.edmp_apex_en0.r2w_en ||
	    cfg.edmp_apex_en1.basic_smd_en || cfg.edmp_apex_en1.soft_hard_iron_corr_en) {
		return (-ENRDY);
	}
	/* Pedometer */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_AMP_TH, (uint8_t *) &p->ped_amp_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_STEP_CNT_TH, (uint8_t *)&p->ped_step_cnt_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_PREV_STEP_CNT_TH, (uint8_t *)&p->ped_step_cnt_th))) { /* same as step_cnt_th */
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_STEP_DET_TH, (uint8_t *)&p->ped_step_det_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_SB_TIMER_TH, (uint8_t *)&p->ped_sb_timer_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_HI_EN_TH, (uint8_t *)&p->ped_hi_en_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_SENSITIVITY_MODE, (uint8_t *)&p->ped_sensitivity_mode))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_PED_LOW_EN_AMP_TH, (uint8_t *)&p->ped_low_en_amp_th))) {
		return (status);
	}
	/* Tilt */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TILT_WAIT_TIME, (uint8_t *)&p->tilt_wait_time))) {
		return (status);
	}
#if ICM_45686_USE_BASIC_SMD == 1
	/* Basic SMD */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_BASICSMD_WIN, (uint8_t *)&p->basicsmd_win))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_BASICSMD_WIN_WAIT, (uint8_t *)&p->basicsmd_win_wait))) {
		return (status);
	}
#else
	/* SMD */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_SMD_SENSITIVITY, (uint8_t *)&p->smd_sensitivity))) {
		return (status);
	}
#endif
	/* R2W */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_SLEEP_TIME_OUT, (uint8_t *)&p->r2w_sleep_time_out))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_SLEEP_GESTURE_DELAY, (uint8_t *)&p->r2w_sleep_gesture_delay))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_MOUNTING_MATRIX, (uint8_t *)&p->r2w_mounting_matrix))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_GRAVITY_FILTER_GAIN, (uint8_t *)&p->r2w_gravity_filter_gain))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_ANGLE_COSINE, (uint8_t *)&p->r2w_motion_th_angle_cosine))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_TIMER_FAST, (uint8_t *)&p->r2w_motion_th_timer_fast))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_MOTION_THR_TIMER_SLOW, (uint8_t *)&p->r2w_motion_th_timer_slow))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_MOTION_PREV_GRAVITY_TIMEOUT, (uint8_t *)&p->r2w_motion_prev_gravity_timeout))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_LAST_GRAVITY_MOTION_TIMER, (uint8_t *)&p->r2w_last_gravity_motion_timer))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_LAST_GRAVITY_TIMEOUT, (uint8_t *)&p->r2w_last_gravity_timeout))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_R2W_GESTURE_VALIDITY_TIMEOUT, (uint8_t *)&p->r2w_gesture_validity_timeout))) {
		return (status);
	}
	/* Free Fall */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_LOWG_PEAK_TH, (uint8_t *)&p->lowg_peak_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_LOWG_PEAK_TH_HYST, (uint8_t *)&p->lowg_peak_th_hyst))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_LOWG_TIME_TH, (uint8_t *)&p->lowg_time_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_HIGHG_PEAK_TH, (uint8_t *)&p->highg_peak_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_HIGHG_PEAK_TH_HYST, (uint8_t *)&p->highg_peak_th_hyst))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_HIGHG_TIME_TH, (uint8_t *)&p->highg_time_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_FF_MIN_DURATION, (uint8_t *)&p->ff_min_duration))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_FF_MAX_DURATION, (uint8_t *)&p->ff_max_duration))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_FF_DEBOUNCE_DURATION, (uint8_t *)&p->ff_debounce_duration))) {
		return (status);
	}
	/* Tap */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_MIN_JERK, (uint8_t *)&p->tap_min_jerk))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_TMAX, (uint8_t *)&p->tap_tmax))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_TMIN, (uint8_t *)&p->tap_tmin))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_MAX_PEAK_TOL, (uint8_t *)&p->tap_max_peak_tol))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_SMUDGE_REJECT_THR, (uint8_t *)&p->tap_smudge_reject_th))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_TAP_TAVG, (uint8_t *)&p->tap_tavg))) {
		return (status);
	}
	/* CalMag */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_SOFT_IRON_SENSITIVITY_MATRIX, (uint8_t *)p->soft_iron_sensitivity_matrix))) {
		return (status);
	}
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_HARD_IRON_OFFSET, (uint8_t *)p->hard_iron_offset))) {
		return (status);
	}
	/* Power save */
	if ((status = ICM_45686_WRITE_EDMP_SRAM(s, EDMP_POWER_SAVE_TIME, (uint8_t *) &p->power_save_time))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &cfg.edmp_apex_en1, 1))) {
		return (status);
	}
	cfg.edmp_apex_en1.power_save_en = p->power_save_en;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &cfg.edmp_apex_en1, 1, TRUE));
}

/**
 * icm_45686_edmp_get_config_int_apex
 */
int icm_45686_edmp_get_config_int_apex(icm_45686 s, icm_45686_edmp_int_state_t *it)
{
	int status;
	icm_45686_int_apex_configx_t cfg;

	if ((status = icm_45686_bank0_read(s, ICM_45686_INT_APEX_CONFIG0, &cfg, 2))) {
		return (status);
	}
	/* INT_APEX_CONFIG0 */
	it->tap = !cfg.int_apex_config0.int_status_mask_pin_tap_detect;
	it->highg = !cfg.int_apex_config0.int_status_mask_pin_high_g_det;
	it->lowg = !cfg.int_apex_config0.int_status_mask_pin_low_g_det;
	it->tilt_det = !cfg.int_apex_config0.int_status_mask_pin_tilt_det;
	it->step_cnt_ovfl = !cfg.int_apex_config0.int_status_mask_pin_step_cnt_ovfl;
	it->step_det = !cfg.int_apex_config0.int_status_mask_pin_step_det;
	it->ff = !cfg.int_apex_config0.int_status_mask_pin_ff_det;
	it->r2w = !cfg.int_apex_config0.int_status_mask_pin_r2w_wake_det;
	/* INT_APEX_CONFIG1 */
	it->r2w_sleep = !cfg.int_apex_config1.int_status_mask_pin_r2w_sleep_det;
#if ICM_45686_USE_BASIC_SMD == 1
	it->smd = !cfg.int_apex_config1.int_status_mask_pin_basic_smd;
#else
	it->smd = !cfg.int_apex_config1.int_status_mask_pin_smd_det;
#endif
	it->self_test = !cfg.int_apex_config1.int_status_mask_pin_selftest_done;
	it->sec_auth = !cfg.int_apex_config1.int_status_mask_pin_sa_done;
	return (0);
}

/**
 * icm_45686_edmp_set_config_int_apex
 */
int icm_45686_edmp_set_config_int_apex(icm_45686 s, const icm_45686_edmp_int_state_t *it)
{
	icm_45686_int_apex_configx_t cfg = {0};

	/* INT_APEX_CONFIG0 */
	cfg.int_apex_config0.int_status_mask_pin_tap_detect = !it->tap;
	cfg.int_apex_config0.int_status_mask_pin_high_g_det = !it->highg;
	cfg.int_apex_config0.int_status_mask_pin_low_g_det = !it->lowg;
	cfg.int_apex_config0.int_status_mask_pin_tilt_det = !it->tilt_det;
	cfg.int_apex_config0.int_status_mask_pin_step_cnt_ovfl = !it->step_cnt_ovfl;
	cfg.int_apex_config0.int_status_mask_pin_step_det = !it->step_det;
	cfg.int_apex_config0.int_status_mask_pin_ff_det = !it->ff;
	cfg.int_apex_config0.int_status_mask_pin_r2w_wake_det = !it->r2w;
	/* INT_APEX_CONFIG1 */
	cfg.int_apex_config1.int_status_mask_pin_r2w_sleep_det = !it->r2w_sleep;
#if ICM_45686_USE_BASIC_SMD == 1
	cfg.int_apex_config1.int_status_mask_pin_basic_smd = !it->smd;
	cfg.int_apex_config1.int_status_mask_pin_smd_det = 1;
#else
	cfg.int_apex_config1.int_status_mask_pin_smd_det = !it->smd;
	cfg.int_apex_config1.int_status_mask_pin_basic_smd = 1;
#endif
	cfg.int_apex_config1.int_status_mask_pin_selftest_done = !it->self_test;
	cfg.int_apex_config1.int_status_mask_pin_sa_done = !it->sec_auth;
	return (icm_45686_bank0_write(s, ICM_45686_INT_APEX_CONFIG0, &cfg, 2, TRUE));
}

/**
 * icm_45686_edmp_enable
 */
int icm_45686_edmp_enable(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1))) {
		return (status);
	}
	edmp_apex_en1.edmp_enable = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1, TRUE));
}

/**
 * icm_45686_edmp_disable
 */
int icm_45686_edmp_disable(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1))) {
		return (status);
	}
	edmp_apex_en1.edmp_enable = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1, TRUE));
}

/*
 * check_dmp_odr_decimation
 *
 * Check if icm_45686_edmp_set_frequency() was called without recomputing APEX decimation
 * thanks to icm_45686_edmp_recompute_apex_decimation().
 * This function will compare edmp decimation rate in ICM SRAM as computed at APEX init step vs the
 * DMP ODR currently written in ICM register.
 * Returns -ENRDY if icm_45686_edmp_recompute_apex_decimation() should have been
 * called.
 */
static int check_dmp_odr_decimation(icm_45686 s)
{
	int status;
	uint8_t dmp_decim_rate_from_sram;
	struct icm_45686_dmp_ext_sen_odr_cfg dmp_ext_sen_odr_cfg;
	enum icm_45686_dmp_ext_sen_odr_cfg_apex_odr apex_odr;

	// edmp decimation rate address in SRAM is 0x460
	if ((status = icm_45686_ireg_read(s, 0x460, &dmp_decim_rate_from_sram, 1))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_DMP_EXT_SEN_ODR_CFG, &dmp_ext_sen_odr_cfg, 1))) {
		return (status);
	}
	apex_odr = (enum icm_45686_dmp_ext_sen_odr_cfg_apex_odr) dmp_ext_sen_odr_cfg.apex_odr;
	switch (dmp_decim_rate_from_sram) {
	case 15:
		if (apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_800_HZ) {
			return (0);
		} else {
			return (-ENRDY);
		}
	case 7:
		if (apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_400_HZ) {
			return (0);
		} else {
			return (-ENRDY);
		}
	case 3:
		if (apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_200_HZ) {
			return (0);
		} else {
			return (-ENRDY);
		}
	case 1:
		if (apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_100_HZ) {
			return (0);
		} else {
			return (-ENRDY);
		}
	case 0:
		if ((apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_50_HZ) ||
		    (apex_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_25_HZ)) {
			return (0);
		} else {
			return (-ENRDY);
		}
	default:
		return (-EHW);
	}
}

/**
 * icm_45686_edmp_enable_pedometer
 */
int icm_45686_edmp_enable_pedometer(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	/* Make sure pedometer is not already enabled to prevent messing up pointers */
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	if (edmp_apex_en0.pedo_en) {
		return (0);
	}
	/* Enable Pedometer */
	edmp_apex_en0.pedo_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_disable_pedometer
 */
int icm_45686_edmp_disable_pedometer(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.pedo_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_enable_smd
 */
int icm_45686_edmp_enable_smd(icm_45686 s)
{
	int status;
#if ICM_45686_USE_BASIC_SMD == 1
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1))) {
		return (status);
	}
	edmp_apex_en1.basic_smd_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1, TRUE));
#else
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.smd_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
#endif
}

/**
 * icm_45686_edmp_disable_smd
 */
int icm_45686_edmp_disable_smd(icm_45686 s)
{
	int status;
#if ICM_45686_USE_BASIC_SMD == 1
	struct icm_45686_edmp_apex_en1 edmp_apex_en1;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1))) {
		return (status);
	}
	edmp_apex_en1.basic_smd_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN1, &edmp_apex_en1, 1, TRUE));
#else
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.smd_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
#endif
}

/**
 * icm_45686_edmp_enable_tilt
 */
int icm_45686_edmp_enable_tilt(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.tilt_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_disable_tilt
 */
int icm_45686_edmp_disable_tilt(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.tilt_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_enable_r2w
 */
int icm_45686_edmp_enable_r2w(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.r2w_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_disable_r2w
 */
int icm_45686_edmp_disable_r2w(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.r2w_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_enable_tap
 */
int icm_45686_edmp_enable_tap(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.tap_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_disable_tap
 */
int icm_45686_edmp_disable_tap(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.tap_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_enable_ff
 */
int icm_45686_edmp_enable_ff(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = check_dmp_odr_decimation(s))) {
		return (status);
	}
	/* Make sure freefall is not already enabled to prevent messing up pointers */
	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	if (edmp_apex_en0.ff_en) {
		return (0);
	}
	/* Enable freefall */
	edmp_apex_en0.ff_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_disable_ff
 */
int icm_45686_edmp_disable_ff(icm_45686 s)
{
	int status;
	struct icm_45686_edmp_apex_en0 edmp_apex_en0;

	if ((status = icm_45686_bank0_read(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1))) {
		return (status);
	}
	edmp_apex_en0.ff_en = ICM_45686_DISABLE;
	return (icm_45686_bank0_write(s, ICM_45686_EDMP_APEX_EN0, &edmp_apex_en0, 1, TRUE));
}

/**
 * icm_45686_edmp_get_int_apex_status
 */
int icm_45686_edmp_get_int_apex_status(icm_45686 s, icm_45686_edmp_int_state_t *it)
{
	int status;
	icm_45686_int_apex_statusx_t st;

	/* Read APEX interrupt status */
	if ((status = icm_45686_bank0_read(s, ICM_45686_INT_APEX_STATUS0, &st, 2))) {
		return (status);
	}
	it->tap = st.int_apex_status0.int_status_tap_det;
	it->highg = st.int_apex_status0.int_status_high_g_det;
	it->lowg = st.int_apex_status0.int_status_low_g_det;
	it->tilt_det = st.int_apex_status0.int_status_tilt_det;
	it->step_cnt_ovfl = st.int_apex_status0.int_status_step_cnt_ovfl;
	it->step_det = st.int_apex_status0.int_status_step_det;
	it->ff = st.int_apex_status0.int_status_ff_det;
	it->r2w = st.int_apex_status0.int_status_r2w_wake_det;
#if ICM_45686_USE_BASIC_SMD == 1
	it->smd = st.int_apex_status1.int_status_basic_smd;
#else
	it->smd = st.int_apex_status1.int_status_smd_det;
#endif
	it->r2w_sleep = st.int_apex_status1.int_status_r2w_sleep_det;
	it->self_test = st.int_apex_status1.int_status_selftest_done;
	it->sec_auth = st.int_apex_status1.int_status_sa_done;
	return (0);
}

/**
 * icm_45686_edmp_get_pedometer_data
 */
int icm_45686_edmp_get_pedometer_data(icm_45686 s, icm_45686_edmp_pedometer_data_t *ped_data)
{
	int status;
	uint8_t data[2];
	int retry = 0;

	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_ACTIVITY_CLASS, data))) {
		return (status);
	}
	ped_data->activity_class = (icm_45686_edmp_activity_class_t) data[0];
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_STEP_CADENCE, data))) {
		return (status);
	}
	ped_data->step_cadence = data[0];
	/*
	 * Always read BUF2 as we forced a buffer full
	 * configuration in `icm_45686_edmp_init_apex()` function.
	 */
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_STEP_CNT_BUF2, data))) {
		return (status);
	}
	/*
	 * Read value multiple times in case the buffer was written while we were reading it.
	 * If we read the same value twice consecutively, it means we have a proper value.
	 */
	while (TRUE) {
		uint8_t data_verif[2];
		if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_PED_STEP_CNT_BUF2, data_verif))) {
			return (status);
		}
		if ((data[0] == data_verif[0]) && (data[1] == data_verif[1])) {
			break;
		}
		data[0] = data_verif[0];
		data[1] = data_verif[1];
		retry++;
		if (retry > 10) {
			return (-EHW);
		}
	}
	ped_data->step_cnt = data[1] << 8 | data[0];
	return (0);
}

/**
 * icm_45686_edmp_get_ff_data
 */
int icm_45686_edmp_get_ff_data(icm_45686 s, uint16_t *freefall_duration)
{
	int status;
	uint8_t data[2];
	struct icm_45686_apex_buffer_mgmt apex_buffer_mgmt;
	uint8_t edmp_wptr, host_rptr;

	if ((status = icm_45686_bank0_read(s, ICM_45686_APEX_BUFFER_MGMT, &apex_buffer_mgmt, 1))) {
		return (status);
	}
	host_rptr = apex_buffer_mgmt.ff_duration_host_rptr;
	edmp_wptr = apex_buffer_mgmt.ff_duration_edmp_wptr;
	if (host_rptr == edmp_wptr) {
		return (-EDATA); // No data.
	}
	if ((host_rptr & 0x1) == 0) {
		if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_FF_DURATION_BUF1, data))) {
			return (status);
		}
	} else {
		if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_FF_DURATION_BUF2, data))) {
			return (status);
		}
	}
	host_rptr = (host_rptr + 1) & 0x03;
	apex_buffer_mgmt.ff_duration_host_rptr = host_rptr;
	if ((status = icm_45686_bank0_write(s, ICM_45686_APEX_BUFFER_MGMT, &apex_buffer_mgmt, 1, TRUE))) {
		return (status);
	}
	*freefall_duration = (data[1] << 8) | data[0];
	return (0);
}

/**
 * icm_45686_edmp_get_tap_data
 */
int icm_45686_edmp_get_tap_data(icm_45686 s, icm_45686_edmp_tap_data_t *data)
{
	int status;
	uint8_t tap_duration = 0;

	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_NUM, &data->num))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_AXIS, &data->axis))) {
		return (status);
	}
	if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_TAP_DIR, &data->direction))) {
		return (status);
	}
	if (data->num == ICM_45686_EDMP_TAP_DOUBLE) {
		if ((status = ICM_45686_READ_EDMP_SRAM(s, EDMP_DOUBLE_TAP_TIMING, &tap_duration))) {
			return (status);
		}
		data->double_tap_timing = (uint16_t) tap_duration * 16;
	} else {
		data->double_tap_timing = 0;
	}
	return (0);
}

/**
 * icm_45686_edmp_mask_int_src
 */
int icm_45686_edmp_mask_int_src(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb, uint8_t int_mask)
{
	int status;
	uint32_t reg_addr;
	uint8_t reg;

	/*
	 * Interrupt mask register for EDMP interrupts are located in 3 consecutive
	 * registers starting with ICM_45686_STATUS_MASK_PIN_0_7 for interrupt0.
	 */
	reg_addr = ICM_45686_STATUS_MASK_PIN_0_7 + edmp_int_nb;
	/* Set bits passed in param to mask corresponding interrupts */
	if ((status = icm_45686_ireg_read(s, reg_addr, &reg, 1))) {
		return (status);
	}
	reg |= int_mask;
	return (icm_45686_ireg_write(s, reg_addr, &reg, 1));
}

/**
 * icm_45686_edmp_unmask_int_src
 */
int icm_45686_edmp_unmask_int_src(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb, uint8_t int_mask)
{
	int status;
	uint32_t reg_addr;
	uint8_t reg;

	/*
	 * Interrupt mask register for EDMP interrupts are located in 3 consecutive
	 * registers starting with STATUS_MASK_PIN_0_7 for interrupt0.
	 */
	reg_addr = ICM_45686_STATUS_MASK_PIN_0_7 + edmp_int_nb;
	/* Clear bits passed in param to unmask corresponding interrupts */
	if ((status = icm_45686_ireg_read(s, reg_addr, &reg, 1))) {
		return (status);
	}
	reg &= ~int_mask;
	return (icm_45686_ireg_write(s, reg_addr, &reg, 1));
}

/**
 * icm_45686_edmp_configure
 */
int icm_45686_edmp_configure(icm_45686 s)
{
	int status;
	uint16_t start_addr[] = {EDMP_ROM_START_ADDR_IRQ0, EDMP_ROM_START_ADDR_IRQ1, EDMP_ROM_START_ADDR_IRQ2};
	/* Only 8 MSB of SP address is written to register */
	uint8_t stack_addr = (uint8_t) (APEX_FEATURE_STACK_END >> 8);
	/* Set Start address for 3 edmp IRQ handlers */
	if ((status = icm_45686_ireg_write(s, ICM_45686_EDMP_PRGRM_IRQ0_0, &start_addr[0], sizeof(start_addr)))) {
		return (status);
	}
	/* Set Stack pointer start address */
	return (icm_45686_ireg_write(s, ICM_45686_EDMP_SP_START_ADDR, &stack_addr, sizeof(stack_addr)));
}

/**
 * icm_45686_edmp_run_ondemand
 */
int icm_45686_edmp_run_ondemand(icm_45686 s, icm_45686_edmp_int_t edmp_int_nb)
{
	int status;
	struct icm_45686_reg_host_msg reg_host_msg;

	if ((status = icm_45686_edmp_unmask_int_src(s, edmp_int_nb, ICM_45686_EDMP_INT_SRC_ON_DEMAND_MASK))) {
		return (status);
	}
	if ((status = icm_45686_edmp_enable(s))) {
		return (status);
	}
	if ((status = icm_45686_bank0_read(s, ICM_45686_REG_HOST_MSG, &reg_host_msg, 1))) {
		return (status);
	}
	reg_host_msg.edmp_on_demand_en = ICM_45686_ENABLE;
	return (icm_45686_bank0_write(s, ICM_45686_REG_HOST_MSG, &reg_host_msg, 1, TRUE));
}

/**
 * icm_45686_edmp_wait_for_idle
 */
int icm_45686_edmp_wait_for_idle(icm_45686 s)
{
	int status;
	struct icm_45686_ipreg_misc ipreg_misc;
	int timeout_us = 1000000;

	/* Wait for idle == 1 (indicates EDMP is not running, e.g execution is completed) */
	while (TRUE) {
		if ((status = icm_45686_ireg_read(s, ICM_45686_IPREG_MISC, &ipreg_misc, 1))) {
			return (status);
		}
		if (ipreg_misc.edmp_idle != 0) {
			return (0);
		}
		delay_us(5);
		timeout_us -= 5;
		if (timeout_us <= 0) {
			return (-ETMO);
		}
	}
}
