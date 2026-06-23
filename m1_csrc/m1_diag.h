/* See COPYING.txt for license details. */

/*
 * m1_diag.h
 *
 * On-device self-diagnostics: reset-cause readout + write-phase breadcrumb
 * that survives a watchdog/brownout/fault reset (stored in .noinit RAM), plus
 * a PVD undervoltage flag. Ported from da-pingwing's "Monstatek M1 RFID Patch"
 * (github.com/da-pingwing/M1_T-1000_RFID, GPL-3.0). This (lean) port keeps the
 * reset-cause + breadcrumb collection and an on-screen readout; the HardFault
 * trampoline and PVD ISR from the original are omitted here.
 *
 * M1 Project
 */

#ifndef M1_DIAG_H_
#define M1_DIAG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write-phase breadcrumb values, stored before each step of the T5577 write. */
typedef enum {
    WRITE_PHASE_NONE    = 0,
    WRITE_PHASE_START   = 1,
    WRITE_PHASE_BLOCK   = 2,
    WRITE_PHASE_PROGRAM = 3,
    WRITE_PHASE_GAP     = 4,
    WRITE_PHASE_RESET   = 5,
    WRITE_PHASE_STOP    = 6,
    WRITE_PHASE_DONE    = 7,
    WRITE_PHASE_UI_CREATE        = 10,
    WRITE_PHASE_WORKER_ENTER     = 11,
    WRITE_PHASE_WORKER_TEARDOWN  = 12,
    WRITE_PHASE_WORKER_PREWRITE  = 13,
    WRITE_PHASE_READBACK         = 14,
    WRITE_PHASE_VERIFY           = 15,
} M1DiagWritePhase;

/* Diag block — placed in .noinit RAM, NOT zeroed by C startup. */
#define M1_DIAG_MAGIC        0xD1A90C0EUL
#define M1_DIAG_FAULT_VALID  0xFA1100FFUL

typedef struct {
    uint32_t magic;
    uint32_t write_count;
    uint8_t  last_write_phase;
    uint8_t  last_write_block;
    uint8_t  pvd_flag;
    uint8_t  reset_src;
    uint32_t fault_valid;
    uint32_t fault_pc;
    uint32_t fault_lr;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t boot_count;
    uint32_t prev_rsr;
    uint32_t prev_fault_pc;
    uint32_t prev_fault_lr;
    uint32_t prev_fault_cfsr;
    uint32_t prev_fault_hfsr;
    uint8_t  prev_phase;
    uint8_t  prev_blk;
    uint8_t  prev_pvd;
    uint8_t  prev_fault;
    uint8_t  prev_reset_src;
} M1DiagBlock;

extern volatile M1DiagBlock m1_diag;

/* Reset-source codes (set before a deliberate reset). */
#define RST_SRC_NONE             0
#define RST_SRC_POWERCTL_REBOOT  1
#define RST_SRC_RPC_REBOOT       2
#define RST_SRC_FWUPDATE         3
#define RST_SRC_HARDFAULT        4
#define RST_SRC_ERROR_HANDLER    5
#define RST_SRC_STACK_OVF        6
#define RST_SRC_MALLOC_FAIL      7
#define RST_SRC_WDT_FAILURE      8
#define RST_SRC_ASSERT           9

/* Read RCC->RSR, snapshot the previous boot's breadcrumb into the prev_*
 * fields, log a boot report, and re-init the block. Call after logdb init. */
void m1_diag_boot_report(void);

/* Dump current + previous diag state to the log (debug UART). */
void m1_diag_dump(void);

/* Blocking on-screen diagnostics view (last reset cause + write phase, etc.),
 * returns on BACK. The readable-without-a-UART-adapter path. */
void m1_diag_screen(void);

/* Breadcrumb setters — ISR-safe single-field writes, used from t5577.c. */
static inline void m1_diag_set_phase(M1DiagWritePhase phase, uint8_t blk)
{
    m1_diag.last_write_phase = (uint8_t)phase;
    m1_diag.last_write_block = blk;
}

static inline void m1_diag_set_reset_src(uint8_t src)
{
    m1_diag.reset_src = src;
}

#ifdef __cplusplus
}
#endif

#endif /* M1_DIAG_H_ */
