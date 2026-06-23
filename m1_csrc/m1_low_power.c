/* See COPYING.txt for license details. */

/*
*
* m1_low_power.c
*
* Library for M1 in low power mode
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "main.h"
#include "cmsis_os.h"
#include "task.h"
#include "m1_low_power.h"

/*************************** D E F I N E S ************************************/

//#define SYSTICK_CURRENT_VALUE_REG		( * ( ( volatile uint32_t * ) 0xe000e018 ) )


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

//static uint32_t tick_1, tick_2;
static TickType_t systick_count_1, systick_count_2;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

/*
 * Setup the timer to generate the tick interrupts.  The implementation in this
 * file is weak to allow application writers to change the timer used to
 * generate the tick interrupt.
 */
void vPortSetupTimerInterrupt( void );

/*
 * Exception handlers.
 */

/*-----------------------------------------------------------*/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
 * This function is called before the device is placed in sleep mode
 *
 */
/*============================================================================*/
void PreSleepProcessing(uint32_t ulExpectedIdleTime)
{
/* place for user code */
	/* Do NOT suspend the HAL tick (TIM6) here. It drives HAL_GetTick(), which
	 * ~40 places in the firmware use for UI/timeout pacing. Suspending it during
	 * tickless idle froze HAL_GetTick() for the whole sleep (it was never
	 * corrected on resume), so HAL_GetTick-based timing drifted badly when the
	 * device idled unplugged -> sluggish UI. Leaving TIM6 running keeps the HAL
	 * clock accurate; its 1 ms IRQ caps each sleep at ~1 ms (light idle sleep). */
	//HAL_SuspendTick();
	//tick_1 = SYSTICK_CURRENT_VALUE_REG;
	systick_count_1 = xTaskGetTickCount();
	//ulExpectedIdleTime = prvGetExpectedIdleTime();
//	HAL_LPTIM_TimeOut_Start_IT(&hlptim1, 0xFFFF, ulExpectedIdleTime);
	// Put the microcontroller into low-power mode
	//HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
	//or
	//HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
	//or
	//HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
	//__WFI();  // Wait for interrupt
} // void PreSleepProcessing(uint32_t ulExpectedIdleTime)



/*============================================================================*/
/*
 * This function is called after the device goes out of sleep mode
 *
 */
/*============================================================================*/
void PostSleepProcessing(uint32_t ulExpectedIdleTime)
{
/* place for user code */
//	HAL_LPTIM_TimeOut_Stop_IT(&hlptim1);
	//tick_2 = SYSTICK_CURRENT_VALUE_REG;
	systick_count_2 = xTaskGetTickCount();
	/* HAL tick (TIM6) was left running in PreSleepProcessing, so no resume is
	 * needed and HAL_GetTick() stayed accurate across the sleep. */
	//HAL_ResumeTick();
} // void PostSleepProcessing(uint32_t ulExpectedIdleTime)



/* vPortSuppressTicksAndSleep() is provided by the CM33 port (port.c) with
 * configUSE_TICKLESS_IDLE == 1: it reprograms SysTick for the expected idle
 * period, enters WFI, and corrects the tick count on wake. It calls
 * PreSleepProcessing()/PostSleepProcessing() above via the
 * configPRE/POST_SLEEP_PROCESSING macros to pause the TIM6 HAL timebase,
 * which would otherwise wake the core every 1 ms. */



/*============================================================================*/
/*
 * Setup the systick timer to generate the tick interrupts at the required
 * frequency.
 */
/*============================================================================*/
/*
void vPortSetupTimerInterrupt( void )
{

} // void vPortSetupTimerInterrupt( void )
*/


#if (USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION == 1)
void SysTick_Handler (void)
{

}
#endif // #if (USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION == 1)

