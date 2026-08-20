# ICM-45686 eDMP/APEX Driver

Embedded C extension for the TDK ICM-45686 eDMP/APEX processing engine.

This project builds on the base ICM-45686 driver and provides access to the embedded APEX algorithms implemented by the device eDMP. It includes support for configuring APEX processing, enabling individual motion-detection features, reading algorithm outputs and handling APEX interrupts.

A FreeRTOS state-machine example is included to demonstrate interrupt-driven tap detection.

## Features

* eDMP/APEX initialization and configuration
* eDMP output data rate configuration
* APEX decimation setup
* APEX parameter read and write access
* Pedometer support
* Significant Motion Detection (SMD)
* Tilt detection
* Raise-to-Wake detection
* Tap and double-tap detection
* Free-fall detection
* APEX interrupt configuration and status handling
* Pedometer, free-fall and tap result retrieval
* eDMP interrupt source masking
* On-demand eDMP execution
* eDMP idle-state monitoring
* Optional FreeRTOS state machine for tap detection

## Files

### `icm45686-edmp.c`

Implementation of the ICM-45686 eDMP/APEX driver.

It provides eDMP initialization, APEX configuration, algorithm enable/disable functions, parameter access, interrupt handling and retrieval of APEX-generated data.

### `icm45686-edmp.h`

Public interface of the eDMP/APEX driver.

It defines the public API, APEX configuration structures, interrupt state representation and output structures for supported algorithms.

### `icm45686-edmp_def.h`

Definitions for the eDMP/APEX memory layout and algorithm parameters.

It contains SRAM addresses and sizes for pedometer, tilt, SMD, Raise-to-Wake, free-fall, tap, calibration, power-save and self-test related data.

### `icm45686_stm_edmp.c`

FreeRTOS state-machine example using the eDMP/APEX interrupt path.

The current implementation configures the sensor for APEX tap detection, applies tap parameters and processes single- and double-tap events reported through the ICM-45686 interrupt output.

### `icm45686_stm_edmp.h`

Public interface of the eDMP state-machine layer.

It provides initialization of the corresponding FreeRTOS sensor task.

## Dependencies

This driver depends on the base ICM-45686 driver:

* `icm45686.c`
* `icm45686.h`
* `icm45686_def.h`

The base driver provides SPI communication and Bank0/IREG access used by the eDMP/APEX layer.

The optional state-machine layer additionally depends on FreeRTOS and project-specific GPIO, pinmux, command-line, diagnostic and LED facilities.

These platform-specific dependencies must be provided by the target project.

## Usage

The eDMP/APEX driver is intended to be used together with the base ICM-45686 driver.

The application initializes the sensor and eDMP, selects the required APEX output data rate, configures algorithm parameters and enables the desired APEX features and interrupt sources.

Individual algorithms may then report events through the APEX interrupt interface, while feature-specific functions provide access to generated data such as pedometer information, free-fall duration or tap details.

The supplied FreeRTOS state-machine layer demonstrates one possible integration using interrupt-driven tap and double-tap detection.

## License

See the license information in the source files.

## Author

Jan Rusnak  
AZTech
