/* See COPYING.txt for license details. */

/*
 * stm32h5xx_hal.h  (HOST TEST SHIM — not the real CubeHAL)
 *
 * flipper_subghz.c includes m1_sub_ghz.h (for S_M1_SubGHz_Band and
 * S_M1_SubGHz_Modulation), which pulls in m1_io_defs.h -> stm32h5xx_hal.h.
 * Shimming the HAL — rather than m1_sub_ghz.h itself — means the host tests
 * compile against the REAL band/modulation enums, so a firmware-side enum
 * change can never silently diverge from what the tests assert.
 *
 * m1_io_defs.h is pure #defines (never expanded in the parser translation
 * unit), so only the handle types named in m1_sub_ghz.h's extern declarations
 * need to exist here.
 *
 * NEVER part of the firmware build.
 *
 * M1 Project — host test harness
 */

#ifndef STM32H5XX_HAL_H_
#define STM32H5XX_HAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Opaque stand-ins for the CubeHAL handle types used in extern declarations. */
typedef struct { int dummy; } EXTI_HandleTypeDef;
typedef struct { int dummy; } TIM_HandleTypeDef;
typedef struct { int dummy; } DMA_HandleTypeDef;

#endif /* STM32H5XX_HAL_H_ */
