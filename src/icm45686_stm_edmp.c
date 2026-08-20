/*
 * icm45686_stm_edmp.c
 *
 * Autors: Jan Rusnak.
 * (c) 2025 AZTech.
 */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <stdio.h>
#include <string.h>
#include <gentyp.h>
#include "sysconf.h"
#include "board.h"
#include <mmio.h>
#include "msgconf.h"
#include "criterr.h"
#include "hwerr.h"
#include "ledui.h"
#include "dlycnt.h"
#include "cmdln.h"
#include "gpio_hal.h"
#include "pinmux_hal.h"
#include "icm45686-edmp.h"
#include "icm45686_stm_edmp.h"

#if ICM45686_SENSOR_MODE == ICM45686_APEX_MODE

#define WAIT_ISR_SIG_TMO_MS 200
#define RESET_STM_TMO_MS 5000

static TaskHandle_t tsk_hndl;
static struct icm_45686_dsc icm_45686_dsc;
static icm_45686 icm = &icm_45686_dsc;
static p_stf_t stmf;
static SemaphoreHandle_t isr_sig;
static unsigned int odr_us;

static struct {
	unsigned int edmp_event_cnt;
	unsigned int unkn_intr_cnt;
	unsigned int unkn_apex_intr_cnt;
	unsigned int stm_rst_cnt;
} stats;

static gfp_t state_sw_reset(void);
static gfp_t state_whoami_check(void);
static gfp_t set_spi_slew(void);
static gfp_t intr_cfg(void);
static gfp_t init_edmp(void);
static gfp_t wait_intr(void);
static gfp_t ping_icm(void);
static BaseType_t int1_isr_clbk(uint32_t status);
static gfp_t state_error(void);
static void tsk(void *p);
static void cmd_icms(void);
static void log_tap_config(icm_45686_edmp_apex_parameters_t *ap);
static char tap_axis_to_ch(icm_45686_edmp_tap_axis_t axis);
static char tap_dir_to_ch(icm_45686_edmp_tap_dir_t dir);

/**
 * init_icm45686_stm_edmp
 */
void init_icm45686_stm_edmp(void)
{
	icm->spi.cfg.spi_bus_id = 0;
	icm->spi.cfg.mode = SPI_HAL_MODE0;
	icm->spi.cfg.bits_trans = SPI_HAL_8_BIT_TRANS;
	icm->spi.cfg.dly_bct_ns = 0;
	icm->spi.cfg.dly_bcs_ns = 20;
	icm->spi.cfg.cs_rise = FALSE;
	icm->spi.cfg.sck_hz = 12000000;
	icm->spi.cfg.csel_num = SPI_HAL_CSEL2;
	icm->spi.cfg.csel_cont = gpio_hal_get_ctrl(ICM45686_CS_CONT_ID);
	icm->spi.cfg.csel_pin = ICM45686_CS_PIN_ID;
	init_icm_45686(icm);
	if (NULL == (isr_sig = xSemaphoreCreateBinary())) {
		crit_err_exit(MALLOC_ERROR);
	}
	add_command_noargs("icms", cmd_icms);
        if (pdPASS != xTaskCreate(tsk, "ICM45686", ICM45686_TASK_STACK_SIZE, NULL,
                                  ICM45686_TASK_PRIO, &tsk_hndl)) {
                crit_err_exit(MALLOC_ERROR);
        }
}

/**
 * state_sw_reset
 */
static gfp_t state_sw_reset(void)
{
	int ret;

	ret = icm_45686_soft_reset(icm);
	if (ret) {
		msg(INF, "ICM45686: Soft reset error (%s)\n", hwerr_str(ret));
		return ((gfp_t) state_error);
	}
	msg(INF, "ICM45686: Soft reset done (ICM45686_APEX_MODE)\n");
	return ((gfp_t) set_spi_slew);
}

/**
 * state_whoami_check
 */
static gfp_t state_whoami_check(void)
{
	int ret;

	ret = icm_45686_whoami_check(icm);
	if (ret) {
		msg(INF, "ICM45686: whoami_check() error (%s)\n", hwerr_str(ret));
		return ((gfp_t) state_error);
	}
	return ((gfp_t) intr_cfg);
}

/**
 * set_spi_slew
 */
static gfp_t set_spi_slew(void)
{
	int ret;

	ret = icm_45686_set_spi_pads_slew(icm, ICM45686_PADS_SPI_SLEW);
	if (ret) {
		msg(INF, "ICM45686: set_spi_slew() error (%s)\n", hwerr_str(ret));
		return ((gfp_t) state_error);
	}
	return ((gfp_t) state_whoami_check);
}

/**
 * intr_cfg
 */
static gfp_t intr_cfg(void)
{
	int ret;
	struct icm_45686_int_bitmap int_cfg = {0};
	struct icm_45686_int_pin_cfg pin_cfg = {0};

	if ((ret = icm_45686_set_config_int_source(icm, ICM_45686_INT1, &int_cfg))) {
		goto err;
	}
#if ICM45686_INTR_TYPE == LOW_LEVEL_INTR_TYPE
	pin_cfg.polarity = ICM_45686_INT1_CONFIG2_POLARITY_LOW;
	pin_cfg.mode = ICM_45686_INT1_CONFIG2_MODE_LATCH;
	pin_cfg.drive = ICM_45686_INT1_CONFIG2_DRIVE_PP;
#elif ICM45686_INTR_TYPE == FALL_EDGE_INTR_TYPE
	pin_cfg.polarity = ICM_45686_INT1_CONFIG2_POLARITY_LOW;
	pin_cfg.mode = ICM_45686_INT1_CONFIG2_MODE_PULSE;
	pin_cfg.drive = ICM_45686_INT1_CONFIG2_DRIVE_PP;
#else
 #error "ICM45686_INTR_TYPE unknown"
#endif
	if ((ret = icm_45686_set_config_int_pin(icm, ICM_45686_INT1, &pin_cfg))) {
		goto err;
	}
	int_cfg.edmp_event = ICM_45686_ENABLE;
	if ((ret = icm_45686_set_config_int_source(icm, ICM_45686_INT1, &int_cfg))) {
		goto err;
	}
	if ((ret = icm_45686_get_int_status(icm, ICM_45686_INT1, &int_cfg))) {
		goto err;
	}
	pinmux_hal_set_func(ICM45686_INT1_CONT, ICM45686_INT1_PIN, PINMUX_HAL_FUNC_GPIO_IN);
	gpio_hal_set_pull(ICM45686_INT1_CONT, ICM45686_INT1_PIN, GPIO_HAL_PULL_NONE);
	gpio_hal_set_filter(ICM45686_INT1_CONT, ICM45686_INT1_PIN, GPIO_HAL_FILTER_NONE);
	gpio_hal_set_schmitt(ICM45686_INT1_CONT, ICM45686_INT1_PIN, TRUE);
#if ICM45686_INTR_TYPE == LOW_LEVEL_INTR_TYPE
	gpio_hal_intr_config(ICM45686_INT1_CONT, ICM45686_INT1_PIN, GPIO_HAL_INTR_LEVEL_LOW);
#elif ICM45686_INTR_TYPE == FALL_EDGE_INTR_TYPE
	gpio_hal_intr_config(ICM45686_INT1_CONT, ICM45686_INT1_PIN, GPIO_HAL_INTR_FALLING);
#else
 #error "ICM45686_INTR_TYPE unknown"
#endif
	if (!gpio_hal_isr_registered(ICM45686_INT1_CONT, int1_isr_clbk)) {
		gpio_hal_isr_register(ICM45686_INT1_CONT, int1_isr_clbk);
	}
	gpio_hal_intr_enable(ICM45686_INT1_CONT, ICM45686_INT1_PIN);
	return ((gfp_t) init_edmp);
err:
	msg(INF, "ICM45686: intr_cfg() error (%s)\n", hwerr_str(ret));
	return ((gfp_t) state_error);
}

/**
 * init_edmp
 */
static gfp_t init_edmp(void)
{
	int ret;
	enum icm_45686_dmp_ext_sen_odr_cfg_apex_odr dmp_odr;
	enum icm_45686_accel_config0_odr accel_odr;
	icm_45686_edmp_apex_parameters_t apex_parameters;
	icm_45686_edmp_int_state_t apex_int_config = {0};

	if ((ret = icm_45686_edmp_init_apex(icm))) {
		goto err;
	}
#if ICM45686_APEX_ODR == ICM45686_APEX_ODR_400HZ
	odr_us = 2500;
	dmp_odr = ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_400_HZ;
	accel_odr = ICM_45686_ACCEL_CONFIG0_ODR_400_HZ;
#else
	odr_us = 1250;
	dmp_odr = ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_800_HZ;
	accel_odr = ICM_45686_ACCEL_CONFIG0_ODR_800_HZ;
#endif
	if ((ret = icm_45686_edmp_set_frequency(icm, dmp_odr))) {
		goto err;
	}
	if ((ret = icm_45686_set_accel_freq(icm, accel_odr))) {
		goto err;
	}
	if ((ret = icm_45686_set_accel_ln_bw(icm, ICM_45686_IPREG_SYS2_REG_131_ACCEL_UI_LPFBW_DIV_4))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_pedometer(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_smd(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_tilt(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_r2w(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_tap(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable_ff(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_disable(icm))) {
		goto err;
	}
	/* Re-initialize APEX */
	if ((ret = icm_45686_edmp_recompute_apex_decimation(icm))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_get_apex_parameters(icm, &apex_parameters))) {
		goto err;
	}
	if (dmp_odr == ICM_45686_DMP_EXT_SEN_ODR_CFG_APEX_ODR_800_HZ) {
		apex_parameters.tap_tmax = ICM_45686_TAP_TMAX_800HZ;
		apex_parameters.tap_tmin = ICM_45686_TAP_TMIN_800HZ;
		apex_parameters.tap_smudge_reject_th = ICM_45686_TAP_SMUDGE_REJECT_THR_800HZ;
	} else {
		apex_parameters.tap_tmax = ICM_45686_TAP_TMAX_400HZ;
		apex_parameters.tap_tmin = ICM_45686_TAP_TMIN_400HZ;
		apex_parameters.tap_smudge_reject_th = ICM_45686_TAP_SMUDGE_REJECT_THR_400HZ;
	}
	/*
	 * TAP_TMIN
	 * Single tap window, sub-windows within Tmax to detect single-tap event.
	 * Unit: Time in sample number.
	 * Range: [24 - 184].
	 */
        apex_parameters.tap_tmin = ICM45686_APEX_TAP_TMIN;
	/*
	 * TAP_TMAX
	 * Size of the analysis window to detect tap events (single-tap or double-tap).
	 * Unit: Time in sample number.
	 * Range: [49 - 496].
	 */
	apex_parameters.tap_tmax = ICM45686_APEX_TAP_TMAX;
	/*
	 * TAP_MIN_JERK
	 * The minimal value of jerk to be considered as a tap candidate.
	 * Unit: g in q6.
	 * Range: [0 - 64].
	 */
	apex_parameters.tap_min_jerk = ICM45686_APEX_TAP_MIN_JERK;
	/*
	 * TAP_SMUDGE_REJECT_THR
	 * Max acceptable number of samples (jerk value) over TAP_MAX_PEAK_TOL
	 * during the Tmin window. Over this value, Tap event is rejected.
	 * Unit: Time in number of samples.
	 * Range: [13 - 92].
	 */
	apex_parameters.tap_smudge_reject_th = ICM45686_APEX_TAP_SMUDGE_REJECT_THR;
	/*
	 * TAP_MAX_PEAK_TOL
	 * Maximum peak tolerance is the percentage of pulse amplitude
	 * to get the smudge threshold for rejection.
	 * Range: [1 (12.5%) 2 (25.0%) 3 (37.5%) 4 (50.0 %)].
         */
	apex_parameters.tap_max_peak_tol = ICM45686_APEX_TAP_MAX_PEAK_TOL;
	/*
	 * APEX_TAP_TAVG
	 * Energy measurement window size to determine the tap axis associated
	 * with the 1st tap.
	 * Unit: Time in sample number.
	 * Range: [1, 2, 4, 8].
	 */
	apex_parameters.tap_tavg = ICM45686_APEX_TAP_TAVG;
	log_tap_config(&apex_parameters);
	if ((ret = icm_45686_edmp_set_apex_parameters(icm, &apex_parameters))) {
		goto err;
	}
	if ((ret = icm_45686_set_accel_mode(icm, ICM_45686_PWR_MGMT0_ACCEL_MODE_LN))) {
		goto err;
	}
	delay_us(ICM_45686_ACC_STARTUP_TIME_US);
	if ((ret = icm_45686_edmp_enable_tap(icm))) {
		goto err;
	}
	apex_int_config.tap = ICM_45686_ENABLE;
	if ((ret = icm_45686_edmp_set_config_int_apex(icm, &apex_int_config))) {
		goto err;
	}
	if ((ret = icm_45686_edmp_enable(icm))) {
		goto err;
	}
	return ((gfp_t) wait_intr);
err:
	msg(INF, "ICM45686: init_apex() error (%s)\n", hwerr_str(ret));
	return ((gfp_t) state_error);
}

/**
 * wait_intr
 */
static gfp_t wait_intr(void)
{
	int ret;
	struct icm_45686_int_bitmap is;
	icm_45686_edmp_int_state_t apex_state;

	if (pdFALSE == xSemaphoreTake(isr_sig, ms_to_os_ticks(WAIT_ISR_SIG_TMO_MS))) {
		return ((gfp_t) ping_icm);
	}
	if ((ret = icm_45686_get_int_status(icm, ICM_45686_INT1, &is))) {
		msg(INF, "ICM45686: get_int_status() error (%s)\n", hwerr_str(ret));
		goto err;
	}
	if (is.edmp_event) {
		stats.edmp_event_cnt++;
		if ((ret = icm_45686_edmp_get_int_apex_status(icm, &apex_state))) {
			msg(INF, "ICM45686: edmp_get_int_apex_status() error (%s)\n", hwerr_str(ret));
			goto err;
		}
		if (apex_state.tap) {
			icm_45686_edmp_tap_data_t tap_data;
			if ((ret = icm_45686_edmp_get_tap_data(icm, &tap_data))) {
				msg(INF, "ICM45686: edmp_get_tap_data() error (%s)\n", hwerr_str(ret));
				goto err;
			}
			if (tap_data.num == ICM_45686_EDMP_TAP_DOUBLE) {
				set_ledui_led_state(LEDUI4, LEDUI_LED_ON);
				msg(INF, "ICM45686: DOUBLE_TAP %c%c %u ms\n",
				    tap_dir_to_ch(tap_data.direction), tap_axis_to_ch(tap_data.axis),
				    tap_data.double_tap_timing * odr_us / 1000);
			} else {
				set_ledui_led_state(LEDUI4, LEDUI_LED_OFF);
				msg(INF, "ICM45686: SINGLE_TAP %c%c\n",
				tap_dir_to_ch(tap_data.direction), tap_axis_to_ch(tap_data.axis));
			}
		} else {
			stats.unkn_apex_intr_cnt++;
		}
	} else {
		stats.unkn_intr_cnt++;
	}
	gpio_hal_intr_enable(ICM45686_INT1_CONT, ICM45686_INT1_PIN);
	return ((gfp_t) wait_intr);
err:
	gpio_hal_intr_enable(ICM45686_INT1_CONT, ICM45686_INT1_PIN);
	return ((gfp_t) state_error);
}

/**
 * ping_icm
 */
static gfp_t ping_icm(void)
{
	int ret;

	ret = icm_45686_whoami_check(icm);
	if (ret) {
		msg(INF, "ICM45686: ping_icm() error (%s)\n", hwerr_str(ret));
		return ((gfp_t) state_error);
	}
	return ((gfp_t) wait_intr);
}

/**
 * int1_isr_clbk
 */
static BaseType_t int1_isr_clbk(uint32_t status)
{
	BaseType_t tsk_wkn = pdFALSE;
#if ICM45686_INTR_TYPE == LOW_LEVEL_INTR_TYPE
	if (status & ICM45686_INT1_PIN &&
	    gpio_hal_is_intr_enabled(ICM45686_INT1_CONT, ICM45686_INT1_PIN) &&
	    gpio_hal_get_input(ICM45686_INT1_CONT, ICM45686_INT1_PIN) == GPIO_HAL_LOW) {
		gpio_hal_intr_disable(ICM45686_INT1_CONT, ICM45686_INT1_PIN);
		xSemaphoreGiveFromISR(isr_sig, &tsk_wkn);
	}
#elif ICM45686_INTR_TYPE == FALL_EDGE_INTR_TYPE
	if (status & ICM45686_INT1_PIN && gpio_hal_is_intr_enabled(ICM45686_INT1_CONT, ICM45686_INT1_PIN) &&
	    gpio_hal_get_input(ICM45686_INT1_CONT, ICM45686_INT1_PIN) == GPIO_HAL_LOW) {
		gpio_hal_intr_disable(ICM45686_INT1_CONT, ICM45686_INT1_PIN);
		xSemaphoreGiveFromISR(isr_sig, &tsk_wkn);
	}
#else
 #error "ICM45686_INTR_TYPE unknown"
#endif
	return (tsk_wkn);
}

/**
 * state_error
 */
static gfp_t state_error(void)
{
	msg(INF, "ICM45686: resetting state machine after error\n");
	vTaskDelay(ms_to_os_ticks(RESET_STM_TMO_MS));
	stats.stm_rst_cnt++;
	return ((gfp_t) state_sw_reset);
}

/**
 * tsk
 */
static void tsk(void *p)
{
	stmf = state_sw_reset;
	while (TRUE) {
		stmf = (p_stf_t) (*stmf)();
	}
}

/**
 * cmd_icms
 */
static void cmd_icms(void)
{
	UBaseType_t pr;

	pr = uxTaskPriorityGet(NULL);
	vTaskPrioritySet(NULL, TASK_PRIO_HIGH);
	msg(INF, cmd_accp);
	msg(INF, "ICM45686 cnt: edmp_event=%u unkn_intr=%u unkn_apex_intr=%u\n",
	    stats.edmp_event_cnt, stats.unkn_intr_cnt, stats.unkn_apex_intr_cnt);
	msg(INF, "ICM45686 cnt: stm_rst=%u\n", stats.stm_rst_cnt);
	vTaskPrioritySet(NULL, pr);
}

/**
 * log_tap_config
 */
static void log_tap_config(icm_45686_edmp_apex_parameters_t *ap)
{
	UBaseType_t pr;

	pr = uxTaskPriorityGet(NULL);
	vTaskPrioritySet(NULL, TASK_PRIO_HIGH);
	msg(INF, "ICM45686: APEX TAP config >>>>>>>>\n");
	msg(INF, "ICM45686: TAP_TMIN=%hhu (0x%02hhX) range: [24 - 184]\n",
	    ap->tap_tmin, ap->tap_tmin);
	msg(INF, "ICM45686: TAP_TMAX=%hu (0x%04hX) range: [49 - 496]\n",
	    ap->tap_tmax, ap->tap_tmax);
	msg(INF, "ICM45686: TAP_MIN_JERK=%hhu (0x%02hhX) range: [0 - 64]\n",
	    ap->tap_min_jerk, ap->tap_min_jerk);
	msg(INF, "ICM45686: TAP_SMUDGE_REJECT_THR=%hhu (0x%02hhX) range: [13 - 92]\n",
	    ap->tap_smudge_reject_th, ap->tap_smudge_reject_th);
	msg(INF, "ICM45686: TAP_MAX_PEAK_TOL=%hhu (0x%02hhX) range: [1 - 4]\n",
	    ap->tap_max_peak_tol, ap->tap_max_peak_tol);
	msg(INF, "ICM45686: TAP_TAVG=%hhu (0x%02hhX) range: [1, 2, 4, 8]\n",
	    ap->tap_tavg, ap->tap_tavg);
	vTaskPrioritySet(NULL, pr);
}

/**
 * tap_axis_to_ch
 */
static char tap_axis_to_ch(icm_45686_edmp_tap_axis_t axis)
{
	if (axis == ICM_45686_EDMP_TAP_AXIS_X) {
		return ('X');
	} else if (axis == ICM_45686_EDMP_TAP_AXIS_Y) {
		return ('Y');
	} else {
		return ('Z');
	}
}

/**
 * tap_dir_to_ch
 */
static char tap_dir_to_ch(icm_45686_edmp_tap_dir_t dir)
{
	if (dir == ICM_45686_EDMP_TAP_DIR_POSITIVE) {
		return ('+');
	} else {
		return ('-');
	}
}
#endif
