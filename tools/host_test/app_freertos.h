/* See COPYING.txt for license details. */

/*
 * app_freertos.h  (HOST TEST SHIM — not the CubeMX FreeRTOS glue)
 *
 * m1_infrared.h (pulled in by Infrared/irsndconfig.h) includes the FreeRTOS
 * application headers. The host tests never run a scheduler; they only need
 * the handle types to exist so the include chain down to the REAL IRSND
 * protocol support flags compiles.
 *
 * NEVER part of the firmware build.
 *
 * M1 Project — host test harness
 */

#ifndef APP_FREERTOS_H_
#define APP_FREERTOS_H_

#include <stdint.h>
#include <stdbool.h>

typedef void         *QueueHandle_t;
typedef void         *TaskHandle_t;
typedef void         *StreamBufferHandle_t;
typedef void         *SemaphoreHandle_t;
typedef unsigned long TickType_t;

#endif /* APP_FREERTOS_H_ */
