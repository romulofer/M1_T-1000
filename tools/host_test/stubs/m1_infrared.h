#ifndef HOST_TEST_M1_INFRARED_H
#define HOST_TEST_M1_INFRARED_H
/*
 * Host shadow of m1_csrc/m1_infrared.h, resolved instead of the real header
 * (which drags in stm32h5xx_hal.h + FreeRTOS) when Infrared/irsndconfig.h:22
 * does #include "m1_infrared.h".
 *
 * Provides ONLY the macros Infrared/irsnd.c and irsndconfig.h consume on host.
 * Values mirror the real m1_csrc/m1_infrared.h. Host-only.
 */

#define IR_ENCODE_CARRIER_PRESCALE_FACTOR   10       /* irsnd_set_freq() prescaler math */
#define IR_ENCODE_TIMER_TX_CHANNEL          4        /* irsndconfig.h ARM_STM32_HAL row (encoder never expands it) */
#define IR_OTA_PULSE_BIT_MASK               0x0001   /* OTA-frame builder: LSB=1 => mark  */
#define IR_OTA_SPACE_BIT_MASK               0xFFFE   /* OTA-frame builder: LSB=0 => space */

#endif /* HOST_TEST_M1_INFRARED_H */
