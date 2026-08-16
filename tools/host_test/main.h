/* See COPYING.txt for license details. */

/*
 * main.h  (HOST TEST SHIM — not Core/Inc/main.h)
 *
 * Infrared/irmpsystem.h includes "main.h" for the CubeMX board definitions.
 * The host tests only need the type declarations to be satisfied so that
 * irsnd.h — and through it the REAL Infrared/irsndconfig.h protocol support
 * flags — can be read on a PC.
 *
 * NEVER part of the firmware build.
 *
 * M1 Project — host test harness
 */

#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

#endif /* MAIN_H_ */
