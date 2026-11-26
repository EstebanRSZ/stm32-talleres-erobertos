/*
 * stm32f4xx_it.c
 * Interrupt service routines
 *
 * Cada vector solo delega en el manejador de la HAL; la HAL limpia las banderas
 * y llama al callback correspondiente, que está en main.c:
 *   ADC_IRQHandler  -> HAL_ADC_IRQHandler  -> HAL_ADC_ConvCpltCallback
 *   TIM3_IRQHandler -> HAL_TIM_IRQHandler  -> HAL_TIM_PeriodElapsedCallback (muestreo, 100 ms)
 *   TIM4_IRQHandler -> HAL_TIM_IRQHandler  -> HAL_TIM_PeriodElapsedCallback (blinky, 250 ms)
 *
 * Los nombres deben coincidir exactamente con la tabla de vectores de
 * startup_stm32f411xx.S.
 */

#include "stm32f4xx_hal.h"

/* Handles definidos en main.c */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

/* SysTick — requerido por la HAL para HAL_GetTick(), HAL_Delay() y los timeouts
   (p. ej. el de 100 ms de HAL_UART_Transmit) */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* ADC1/ADC2/ADC3 comparten esta línea; aquí solo se usa ADC1 */
void ADC_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&hadc1);
}

/* TIM3 update event — periodo de muestreo del joystick (100 ms) */
void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim3);
}

/* TIM4 update event — blinky del LED PH1 (250 ms) */
void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim4);
}
