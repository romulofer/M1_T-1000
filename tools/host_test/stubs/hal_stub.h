#ifndef HOST_TEST_HAL_STUB_H
#define HOST_TEST_HAL_STUB_H
/*
 * Minimal host stand-in for the STM32 HAL symbols that Infrared/irsnd.c
 * references. Only irsnd_set_freq()/irsnd_on()/irsnd_off() touch the HAL, and
 * the on-air oracle (test_ir_tx_frames.c) never calls those at runtime — these
 * declarations exist so the REAL irsnd.c compiles and links on host.
 *
 * Host-only. Never included by the firmware build.
 */
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;

/* TIM handle — only the fields irsnd_set_freq() assigns. */
typedef struct {
	uint32_t Prescaler;
	uint32_t CounterMode;
	uint32_t Period;
	uint32_t ClockDivision;
	uint32_t AutoReloadPreload;
	uint32_t RepetitionCounter;
} TIM_Base_InitTypeDef;

typedef struct {
	void                *Instance;   /* only referenced in a commented-out line */
	TIM_Base_InitTypeDef Init;
} TIM_HandleTypeDef;

typedef struct {
	uint32_t MasterOutputTrigger;
	uint32_t MasterSlaveMode;
} TIM_MasterConfigTypeDef;

typedef struct {
	uint32_t OCMode;
	uint32_t Pulse;
	uint32_t OCPolarity;
	uint32_t OCNPolarity;
	uint32_t OCFastMode;
	uint32_t OCIdleState;
	uint32_t OCNIdleState;
} TIM_OC_InitTypeDef;

/* Constants assigned in irsnd_set_freq(); values are irrelevant on host. */
#define TIM_COUNTERMODE_UP              0u
#define TIM_CLOCKDIVISION_DIV1          0u
#define TIM_AUTORELOAD_PRELOAD_DISABLE  0u
#define TIM_TRGO_RESET                  0u
#define TIM_MASTERSLAVEMODE_DISABLE     0u
#define TIM_OCMODE_PWM1                 0u
#define TIM_OCPOLARITY_HIGH             0u
#define TIM_OCFAST_DISABLE              0u
#define TIM_OCNIDLESTATE_RESET          0u

/* No-op HAL API used by irsnd.c (defined in hal_stub.c). */
uint32_t          HAL_RCC_GetPCLK2Freq(void);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, TIM_MasterConfigTypeDef *cfg);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, TIM_OC_InitTypeDef *cfg, uint32_t ch);
void              HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *htim, uint32_t ch);
void              HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef *htim, uint32_t ch);
void              Error_Handler(void);

#endif /* HOST_TEST_HAL_STUB_H */
