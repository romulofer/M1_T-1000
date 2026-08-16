/* See COPYING.txt for license details. */

/*
 * cmsis_os.h  (HOST TEST SHIM — not CMSIS-RTOS v2)
 *
 * m1_compile_cfg.h includes "cmsis_os.h" and refuses to compile unless
 * osCMSIS reports at least version 2.0, so the shim declares the version it
 * checks for. No RTOS API is provided — the host tests never start one.
 *
 * NEVER part of the firmware build.
 *
 * M1 Project — host test harness
 */

#ifndef CMSIS_OS_H_
#define CMSIS_OS_H_

#include "app_freertos.h"

#define osCMSIS   0x20001   /* CMSIS-RTOS v2.1, matching the firmware build */

#endif /* CMSIS_OS_H_ */
