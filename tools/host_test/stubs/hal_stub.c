/*
 * No-op definitions for the HAL symbols Infrared/irsnd.c calls from
 * irsnd_set_freq()/irsnd_on()/irsnd_off(). The on-air oracle only exercises
 * irsnd_generate_tx_data() and irsnd_init(), neither of which touches the HAL,
 * so these merely satisfy the linker for the compiled-but-unexercised paths.
 *
 * Host-only. Never compiled into the firmware.
 */
#include "hal_stub.h"

uint32_t HAL_RCC_GetPCLK2Freq(void) { return 64000000u; }

HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim) { (void)htim; return HAL_OK; }

HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, TIM_MasterConfigTypeDef *cfg)
{ (void)htim; (void)cfg; return HAL_OK; }

HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, TIM_OC_InitTypeDef *cfg, uint32_t ch)
{ (void)htim; (void)cfg; (void)ch; return HAL_OK; }

void HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *htim, uint32_t ch) { (void)htim; (void)ch; }

void HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef *htim, uint32_t ch) { (void)htim; (void)ch; }

void Error_Handler(void) { }
