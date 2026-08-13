# ICM-45686 eDMP

Driver for the ICM-45686 embedded Digital Motion Processor (eDMP) and APEX motion-processing features.

## Description

The module provides access to the ICM-45686 eDMP and APEX functionality on top of the main ICM-45686 driver.

It supports eDMP initialization and configuration, APEX parameter access, interrupt configuration and status handling, and control of individual motion-processing algorithms.

Supported APEX features include:

- pedometer and activity classification,
- Significant Motion Detection (SMD),
- tilt detection,
- Raise-to-Wake (R2W),
- tap and double-tap detection,
- free-fall detection,
- high-g and low-g event handling,
- magnetometer soft-iron and hard-iron calibration parameters,
- eDMP power-save configuration.

The driver also provides access to APEX output data such as step count, step cadence, activity class, free-fall duration and tap information.

## Configuration

`ICM_45686_USE_BASIC_SMD` selects the Significant Motion Detection implementation:

- `0` - standard SMD,
- `1` - Basic SMD.

If not defined by the application, standard SMD is selected.

The eDMP output data rate is configured with `icm_45686_edmp_set_frequency()`. After changing the frequency, APEX decimation must be recomputed before enabling APEX algorithms.

## Files

- `icm45686-edmp.c` - eDMP/APEX driver implementation.
- `icm45686-edmp.h` - public API, data types and configuration.
- `icm45686-edmp_def.h` - eDMP SRAM addresses, parameter sizes and related definitions.

## Dependencies

The module depends on the main ICM-45686 driver and project support modules providing:

- register and indirect-register access,
- delay functions,
- hardware error codes,
- basic project types and configuration.

## Author

Jan Rusnak  
Copyright (c) 2025 AZTech
