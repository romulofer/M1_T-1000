#ifndef HOST_TEST_MAIN_H
#define HOST_TEST_MAIN_H
/*
 * Host shadow of the STM32Cube "main.h" that Infrared/irmpsystem.h pulls in
 * (irmpsystem.h:25). Supplies only the HAL types/stubs the encoder needs; no
 * real pins, clocks, or peripherals.
 *
 * Resolved instead of Core/Inc/main.h because tools/host_test/stubs is placed
 * ahead of the firmware include dirs. Host-only.
 */
#include <stdint.h>
#include "hal_stub.h"

#endif /* HOST_TEST_MAIN_H */
