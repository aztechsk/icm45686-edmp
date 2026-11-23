# ICM-45686 eDMP/APEX Driver

Embedded C extension for the TDK InvenSense ICM-45686 embedded Digital Motion Processor
(eDMP) and APEX motion-detection algorithms. It uses the separate `icm45686` LL driver
for SPI, register access, reset and the physical interrupt interface.

The library provides a configurable eDMP/APEX low-level (LL) API and one optional FreeRTOS
tap-detection state machine (STM). The LL API exposes multiple APEX features; the supplied
STM intentionally enables **only single- and double-tap detection** with a fixed acquisition
sequence and client-configured tap thresholds.

## Features

- eDMP ROM/SRAM initialization and processing output data rate (ODR) selection
- APEX decimation initialization, parameter read/write and power-save configuration
- Pedometer, Significant Motion Detection (SMD), tilt, raise-to-wake (R2W), tap and free-fall control
- APEX interrupt masking, status reading and physical INT1/INT2 routing through the base LL
- Pedometer results, buffered free-fall duration and single/double-tap details
- Internal eDMP IRQ0/IRQ1/IRQ2 source masking and on-demand execution
- Optional FreeRTOS STM for continuous, INT1-driven tap detection
- GPIO edge/level interrupt modes, recovery and per-cause diagnostic counters in the STM

## Files

| File | Purpose |
| --- | --- |
| `src/icm45686-edmp.c` | eDMP/APEX initialization, configuration, algorithms and results |
| `src/icm45686-edmp.h` | Public LL API, APEX parameter and result types, calling contracts |
| `src/icm45686-edmp_def.h` | eDMP SRAM addresses, sizes and parameter documentation |
| `src/icm45686_stm_edmp.c` | FreeRTOS INT1 tap-detection STM and diagnostic command |
| `src/icm45686_stm_edmp.h` | STM startup, configuration, IRQ, recovery and diagnostics documentation |

## Using the eDMP/APEX low-level API

The eDMP LL functions supplement, rather than replace, the base `icm45686` API. Use a
single initialized `icm_45686` device descriptor. Calls are synchronous SPI/IREG operations,
not ISR-safe; one task must serialize access to the sensor. Do not access the device in
parallel with an STM that owns it.

### Initialization and output data rate

The LL supports eDMP ODR selections of **25, 50, 100, 200, 400 or 800 Hz** via
`icm_45686_edmp_set_frequency()`. Accelerometer ODR must be at least the selected eDMP ODR.
The application must choose a compatible accelerometer power mode: eDMP at 800 Hz requires
Low-Noise mode; accelerometer Low-Power mode is supported through 400 Hz subject to the
base driver's power/clock restrictions. The **tap algorithm** supports eDMP ODR 200, 400
or 800 Hz, not every LL eDMP ODR.

An LL-based application performs sensor reset/configuration and eDMP setup explicitly:

1. Initialize the base SPI device with `init_icm_45686()` and reset/identify the sensor.
2. Keep eDMP disabled, select the desired eDMP ODR and call `icm_45686_edmp_init_apex()`.
3. Configure compatible accelerometer ODR, filter and power mode using the base LL.
4. If needed, recompute APEX decimation with `icm_45686_edmp_recompute_apex_decimation()`.
5. Read defaults with `icm_45686_edmp_get_apex_parameters()`, modify relevant fields,
   then write them with `icm_45686_edmp_set_apex_parameters()` while algorithms are disabled.
6. Enable the intended APEX algorithms, configure their event masks, route the eDMP event
   to a physical interrupt pin with the base LL, and enable eDMP processing.

The precise ordering and all API preconditions are documented in `src/icm45686-edmp.h`.
`init_apex()` clears and restores eDMP SRAM algorithm defaults. Recomputing decimation
also resets parameters, so applications must restore any custom values afterwards.
Neither operation preserves an arbitrary prior internal eDMP interrupt mask.

### APEX algorithms and result data

| Feature | LL interface | Result / notes |
| --- | --- | --- |
| Pedometer | `icm_45686_edmp_enable_pedometer()` | Step count, encoded cadence, WALK/RUN/UNKNOWN class |
| SMD | `icm_45686_edmp_enable_smd()` | APEX significant-motion interrupt |
| Tilt | `icm_45686_edmp_enable_tilt()` | APEX tilt-detection interrupt |
| R2W | `icm_45686_edmp_enable_r2w()` | Raise-to-wake and sleep events |
| Tap | `icm_45686_edmp_enable_tap()` | Single/double type, X/Y/Z axis, direction and interval |
| Free-fall | `icm_45686_edmp_enable_ff()` | Buffered free-fall duration in accel samples |

Each algorithm has a corresponding disable function. Enable/disable functions change
feature bits; they do not themselves configure accelerometer power, eDMP execution or
physical interrupt routing. `ICM_45686_USE_BASIC_SMD` (default `0`) selects the SMD
variant and associated parameter layout when the LL is compiled.

`icm_45686_edmp_get_int_apex_status()` reads and clears the APEX status registers.
`icm_45686_edmp_set_config_int_apex()` unmasks selected APEX events when their structure
members are nonzero; it does not configure physical INT1/INT2 by itself. The separate
`icm_45686_edmp_mask_int_src()` and `_unmask_int_src()` functions address internal
eDMP IRQ0..IRQ2 input-source masks, **not** the external INT1/INT2 pins.

`icm_45686_edmp_get_pedometer_data()` checks for a stable step count. The free-fall read
consumes a buffered result and can return `-EDATA` when no duration is available.
`icm_45686_edmp_get_tap_data()` returns direction 0=negative, 1=positive as in the
TDK reference C implementation. For double taps, `double_tap_timing` is an interval in
accelerometer samples, quantized in multiples of 16; divide by accelerometer ODR (Hz)
for seconds. It is zero for single taps.

The APEX parameter structure stores values in device-specific units and fixed-point
formats (for example g/Q6 tap jerk, g/Q12 LowG/HighG thresholds). The driver does not
validate all allowed parameter ranges or roll back partially completed SRAM writes.
See `src/icm45686-edmp_def.h` and TDK AN-000478 for parameter definitions.

The LL also exposes direct eDMP SRAM read/write macros, `icm_45686_edmp_configure()`,
internal on-demand execution and `icm_45686_edmp_wait_for_idle()`. They are intended
for callers that manage the complete initialization and interrupt sequences themselves.

## Using the tap-detection state machine

Select `ICM45686_APEX_MODE` using `ICM45686_SENSOR_MODE` in the parent project's
`sysconf.h`, and call `init_icm45686_stm_edmp()` **once** during startup. The function
creates the SPI descriptor, binary semaphore, `icms` command and FreeRTOS task.
The task then resets and configures the sensor and processes continuous eDMP tap events.
The same sensor must not be controlled concurrently from application LL code.

### Fixed STM sensor configuration

| Setting | eDMP STM |
| --- | --- |
| SPI | Bus 0, mode 0, 8-bit, 12 MHz, CSEL2; 20 ns CS setup; no CS rise between segments |
| Accelerometer | Low-Noise, 400 or 800 Hz (compile-time), UI LPF ODR/4 |
| Gyroscope and FIFO | Not used |
| eDMP | APEX initialized; decimation recomputed; tap enabled, other algorithms disabled |
| INT1 | Active LOW, push-pull; eDMP event source, pulse/edge or latch/level |
| Acquisition wait | Binary semaphore, 200 ms timeout |
| Timeout fallback | WHO_AM_I check; absence of taps is not an error |
| Tap output | `msg()` single/double-tap text; LEDUI4 OFF/ON respectively |
| Recovery | INT1 mask/disarm, semaphore drain, reset and reconfigure; optional retry backoff |

The task uses no sensor FIFO, raw accel/gyro measurement reports, application callbacks
or internal delivery queue. There is no watchdog for time without a tap: the sensor can
correctly remain motionless indefinitely. A successful 200 ms wait timeout simply leads
to the WHO_AM_I check and another wait.

### TAP configuration and physical time

The six APEX tap parameters are **client-defined**, not automatically recalculated when
switching between 400 and 800 Hz. Their encoded values are programmed after the eDMP
defaults are loaded. Select suitable values in `sysconf.h`:

| Macro | Meaning | Allowed values |
| --- | --- | --- |
| `ICM45686_APEX_TAP_TMIN` | Single-tap analysis sub-window (samples) | 24..184 |
| `ICM45686_APEX_TAP_TMAX` | Analysis window (samples) | 49..496 |
| `ICM45686_APEX_TAP_MIN_JERK` | Minimum candidate jerk (g/Q6) | 0..64 |
| `ICM45686_APEX_TAP_SMUDGE_REJECT_THR` | Maximum over-threshold samples in Tmin | 13..92 |
| `ICM45686_APEX_TAP_MAX_PEAK_TOL` | Peak tolerance selector | 1..4 |
| `ICM45686_APEX_TAP_TAVG` | Axis energy window (samples) | 1, 2, 4, 8 |

For example, `TMIN=66` means 165 ms at 400 Hz and 82.5 ms at 800 Hz. No range checks
are performed by this STM. Programmed tap parameters and their documented ranges are
logged during every eDMP initialization.

On a double tap, the STM reports time in milliseconds using:

`interval_ms = double_tap_timing * 1000 / accelerometer_ODR_Hz`

The multiplication precedes integer division, as in the STM implementation. For
a single tap, no interval is reported. The direction symbol `+`/`-` follows the TDK
reference C driver rather than the conflicting polarity description in DS-000577.

## Build-time configuration (`sysconf.h` and `board.h`)

| Setting | Meaning |
| --- | --- |
| `ICM45686_SENSOR_MODE` | Must select `ICM45686_APEX_MODE`; both macros must be defined |
| `ICM45686_APEX_ODR` | `ICM45686_APEX_ODR_400HZ` or `ICM45686_APEX_ODR_800HZ` |
| `ICM45686_TASK_PRIO` | FreeRTOS task priority |
| `ICM45686_TASK_STACK_SIZE` | Task stack depth in FreeRTOS stack units |
| `ICM45686_INTR_TYPE` | `FALL_EDGE_INTR_TYPE` or `LOW_LEVEL_INTR_TYPE` |
| `ICM45686_RESET_STM_TMO_MS` | Optional recovery delay in milliseconds |
| `ICM45686_PADS_SPI_SLEW` | Sensor SPI pad slew selector after reset |
| `ICM45686_APEX_TAP_*` | Six tap parameters in the table above |
| `ICM_45686_USE_BASIC_SMD` | LL SMD variant, default `0`; SMD is disabled in this STM |
| `ICM45686_CS_CONT_ID` / `ICM45686_CS_PIN_ID` | SPI chip-select controller and pin mask |
| `ICM45686_INT1_CONT` / `ICM45686_INT1_PIN` | INT1 GPIO controller and pin mask |

The APEX ODR selector uses client-defined numeric constants; the implementation rejects
other selections. `ICM45686_RESET_STM_TMO_MS` is **optional**: when absent, recovery
attempts reset immediately. When defined, the value must be positive and convert to at
least one FreeRTOS tick through `ms_to_os_ticks()`. This is a retry backoff, not part
of sensor reset timing. Configuration and board mappings are supplied by the parent
project; this library does not provide `sysconf.h` or `board.h`.

For either INT1 mode, the GPIO uses no pull or filter and its Schmitt trigger is enabled:

- `FALL_EDGE_INTR_TYPE`: sensor PULSE mode, GPIO falling edge; a captured edge is accepted
  even if the pin has already returned HIGH (the default pulse width is 8 us).
- `LOW_LEVEL_INTR_TYPE`: sensor LATCH mode, GPIO active-low level; ISR checks the LOW pin.

INT1 is armed and enabled only near the end of eDMP initialization, before routing the
sensor event source and enabling the eDMP processor. Recovery masks INT1, disarms the
ISR and drains any old binary-semaphore signal on the next reset. This follows the
interrupt-lifetime model used by the base ICM-45686 FIFO STM.

The base module's `ICM45686_REPORT_*` switches and `ICM45686_FIFO_HIRES` do **not**
control this STM. To change the hard-coded SPI parameters, LPF, physical routing,
algorithm selection or output mechanism, modify `icm45686_stm_edmp.c` and rebuild.

## Errors and diagnostics

Any failed sensor reset, identification, SPI/INT1 configuration, eDMP setup, interrupt
status or tap-data read transitions to recovery. Recovery masks GPIO INT1, disarms
its ISR, optionally waits for `ICM45686_RESET_STM_TMO_MS`, then resets and reinitializes
the sensor. Task/semaphore allocation failures during initial startup call
`crit_err_exit()`. There is no application callback for failed reads.

The `icms` command provides two normal diagnostic lines:

- `edmp_event`: INT1 status indicated an eDMP event; not necessarily a confirmed tap.
- `unkn_intr`: a semaphore wakeup without eDMP event status.
- `unkn_apex_intr`: an eDMP event without a tap flag in APEX status.
- `irq_pin_high`: accepted falling-edge ISR events with the pin already HIGH; not a loss counter.
- `stale_irq`: GPIO ISR events received while acquisition is disarmed.
- `stm_rst`: completed recovery cycles, not the initial startup reset.

Two additional `ICM45686 STM err` lines identify fatal causes:

| Counter | Failure source |
| --- | --- |
| `sw_reset` | Sensor soft reset |
| `probe` | WHO_AM_I during initialization or 200 ms timeout fallback |
| `cfg` | SPI pad slew, INT1 configuration or initial status read |
| `edmp_init` | eDMP/APEX initialization, parameters, sensor mode or enable sequence |
| `int_stat` | INT1 status read after ISR semaphore |
| `apex_stat` | APEX interrupt status read |
| `tap_data` | Tap result read from eDMP SRAM |

Each fatal transition increments its corresponding cause counter. `stm_rst` increments
after any configured recovery delay, so it can temporarily lag the fatal counters.
Nonfatal unknown-event and pin-level counters do not cause resets. All counters survive
STM recovery; they reset when the firmware restarts.

## Dependencies

This module requires the base `icm45686` driver (`icm45686.c`, `icm45686.h` and
`icm45686_def.h`) for SPI/IREG access, register configuration and interrupt control.
The eDMP LL additionally uses the project's timing and error-handling services.
The STM depends on FreeRTOS tasks/semaphores, SPI/GPIO/pinmux HALs, board definitions,
command registration, `msg()` output and LEDUI. `sysconf.h`, `board.h` and their
backends come from the parent firmware.

**Compiler ABI:** Base register descriptors use C bit-fields; bit allocation order is
implementation-defined. The target ABI must match the LSB-first bit-field layout used
by this implementation. Multi-byte eDMP SRAM parameters are transferred in the
native little-endian representation expected by the ARM target. The tap result
reader converts 1-byte SRAM values into C enum objects and does not depend on
`sizeof(enum)` for those reads. Verify these ABI assumptions on another toolchain.

## Reference

- TDK InvenSense ICM-45686 datasheet **DS-000577**, revision 1.0.
- TDK **AN-000478**, ICM-45605/ICM-45686 eDMP/APEX User Guide.
- TDK InvenSense reference C driver (`inv_imu_edmp.c`, `inv_imu_edmp.h`,
  `inv_imu_edmp_defs.h`); the tap direction and 16-sample timing decoding
  follow its implementation where device documentation differs.

## License

See the license information in the source files.

## Author

Jan Rusnak  
AZTech
