/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Esteban Roberto Saiz
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "stm32f411xe.h"
#include <stm32f4xx.h> // Agregamos la librería de cabecera del microcontrolador STM32F4
#include <stdint.h>


uint8_t estado_actual = 0; // Variable para almacenar el estado actual del LED (0: apagado, 1: encendido)

int main(void)
{

  

    /*Habilitamos el reloj del puerto GPIOB y configuramos el pin 5 como salida para controlar el LED.*/
    RCC->AHB1ENR &= ~RCC_AHB1ENR_GPIOBEN; // Deshabilitamos el reloj del puerto GPIOB para tener buenas practicas. Esta funcion shiftea el bit del registro AHB1ENR para deshabilitar el reloj del puerto GPIOB, ya que manualmente se haria (1 << 1) y se hace un AND con el registro AHB1ENR para deshabilitar el reloj del puerto GPIOB.
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; // Habilitamos el reloj del puerto GPIOB, esta funcion shiftea el bit del registro AHB1ENR para habilitar el reloj del puerto GPIOB, ya que manualmente se haria (1 << 1) y se hace un OR con el registro AHB1ENR para habilitar el reloj del puerto GPIOB.

    /* CONFIGURACION DEL PIN8 -> Verde*/

    /* Vamos a configurar el PIN8 como salida al pin GPIOB */
    GPIOB->MODER  &= ~GPIO_MODER_MODE8; // Limpiamos el registro MODER del pin 8 del puerto GPIOB para configurar el pin como salida. Esta funcion hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 8, ya que manualmente se haria (1 << 16) y (1 << 17) y se hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 8.
    GPIOB->MODER  |= GPIO_MODER_MODE8_0; // Configuramos el pin 8 del puerto GPIOB como salida. Entonces como ya limpiamos los 2 bits del registro y queremos poner un `01` entonces solo basta poner el `1` que estaria en la posicion 0, por eso la funcion contiene el "_0". Si quisieramos poner un `10` pondriamos `..MODER_1`.

    /*Configuramos el tipo de Salida*/
    GPIOB->OTYPER  &= ~GPIO_OTYPER_OT8; // Configuramos el pin 8 del puerto GPIOB como salida push-pull. Entonces se pone un AND negado para asegurar que se tenga un 0. 
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8; // Configuramos la velocidad del pin 8 del puerto GPIOB como baja que es `00`. Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->PUPDR   &= ~GPIO_PUPDR_PUPD8; // Configuramos el pin 8 del puerto GPIOB como sin pull-up ni pull-down (`00`). Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->ODR     &= ~GPIO_ODR_OD8; // Inicializamos el pin 8 del puerto GPIOB en bajo (apagado). Entonces se pone un AND negado para asegurar que se tenga un 0.

    
    
    /* CONFIGURACION DEL PIN9 -> Amarillo*/

    /* Vamos a configurar el PIN9 como salida al pin GPIOB */
    GPIOB->MODER  &= ~GPIO_MODER_MODE9; // Limpiamos el registro MODER del pin 9 del puerto GPIOB para configurar el pin como salida. Esta funcion hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 8, ya que manualmente se haria (1 << 16) y (1 << 17) y se hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 9.
    GPIOB->MODER  |= GPIO_MODER_MODE9_0; // Configuramos el pin 9 del puerto GPIOB como salida. Entonces como ya limpiamos los 2 bits del registro y queremos poner un `01` entonces solo basta poner el `1` que estaria en la posicion 0, por eso la funcion contiene el "_0". Si quisieramos poner un `10` pondriamos `..MODER_1`.

    /*Configuramos el tipo de Salida*/
    GPIOB->OTYPER  &= ~GPIO_OTYPER_OT9; // Configuramos el pin 9 del puerto GPIOB como salida push-pull. Entonces se pone un AND negado para asegurar que se tenga un 0. 
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9; // Configuramos la velocidad del pin 9 del puerto GPIOB como baja que es `00`. Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->PUPDR   &= ~GPIO_PUPDR_PUPD9; // Configuramos el pin 9 del puerto GPIOB como sin pull-up ni pull-down (`00`). Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->ODR     &= ~GPIO_ODR_OD9; // Inicializamos el pin 9 del puerto GPIOB en bajo (apagado). Entonces se pone un AND negado para asegurar que se tenga un 0.




    /* CONFIGURACION DEL PIN6 -> Rojo*/


    /* Vamos a configurar el PIN6 como salida al pin GPIOB */
    GPIOB->MODER  &= ~GPIO_MODER_MODE6; // Limpiamos el registro MODER del pin 6 del puerto GPIOB para configurar el pin como salida. Esta funcion hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 6, ya que manualmente se haria (1 << 16) y (1 << 17) y se hace un AND con el registro MODER del puerto GPIOB para limpiar los bits correspondientes al pin 6.
    GPIOB->MODER  |= GPIO_MODER_MODE6_0; // Configuramos el pin 6 del puerto GPIOB como salida. Entonces como ya limpiamos los 2 bits del registro y queremos poner un `01` entonces solo basta poner el `1` que estaria en la posicion 0, por eso la funcion contiene el "_0". Si quisieramos poner un `10` pondriamos `..MODER_1`.

    /*Configuramos el tipo de Salida*/
    GPIOB->OTYPER  &= ~GPIO_OTYPER_OT6; // Configuramos el pin 6 del puerto GPIOB como salida push-pull. Entonces se pone un AND negado para asegurar que se tenga un 0. 
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6; // Configuramos la velocidad del pin 6 del puerto GPIOB como baja que es `00`. Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->PUPDR   &= ~GPIO_PUPDR_PUPD6; // Configuramos el pin 6 del puerto GPIOB como sin pull-up ni pull-down (`00`). Entonces se pone un AND negado para asegurar que se tenga un 00.
    GPIOB->ODR     &= ~GPIO_ODR_OD6; // Inicializamos el pin 6 del puerto GPIOB en bajo (apagado). Entonces se pone un AND negado para asegurar que se tenga un 0.




    /* Configuracion del SYSTICK*/
    SysTick->LOAD = 16000000 - 1; // Configuramos el valor de carga del SYSTICK para generar una interrupcion cada 1 segundo. El valor de carga se calcula como (Tiempo deseado * Frecuencia del reloj del sistema) - 1. En este caso, queremos un tiempo de 1 segundo y la frecuencia del reloj del sistema es de 16 MHz, por lo que el valor de carga es (1 * 16000000) - 1 = 15999999.
    SysTick->VAL = 0; // Reiniciamos el contador del SYSTICK
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk; // Deshabilitamos la interrupcion del SYSTICK
    SysTick->CTRL |= SysTick_CTRL_CLKSOURCE_Msk; // Seleccionamos la fuente de reloj del SYSTICK como el reloj del sistema (HCLK)
    
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // Deshabilitamos el SYST para asegurarnos de que no este habilitado antes de configurarlo
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // Habilitamos el SYSTICK

   while(1){


    if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) 
    { // Verificamos si la bandera de conteo del SYSTICK está activa, lo que indica que ha transcurrido el tiempo configurado (1 segundo en este caso)


        switch (estado_actual) 
        {

            case 0: // Estado 1: LED Verde encendido
                GPIOB->ODR |= GPIO_ODR_OD8; // Encendemos el LED verde (pin 8)
                GPIOB->ODR &= ~GPIO_ODR_OD6; // Apagamos el LED rojo (pin 6)
                GPIOB->ODR &= ~GPIO_ODR_OD9; // Apagamos el LED amarillo (pin 9)
                estado_actual ++; // Le sumamos 1 al estado actual para pasar al siguiente estado
                break;

            case 1: // Estado 2: LED amarillo encendido
                GPIOB->ODR &= ~GPIO_ODR_OD8; // Apagamos el LED verde (pin 8)
                GPIOB->ODR |= GPIO_ODR_OD9; // Encendemos el LED amarillo (pin 9)
                GPIOB->ODR &= ~GPIO_ODR_OD6; // Apagamos el LED rojo (pin 6)
                estado_actual ++; // Le sumamos 1 al estado actual para pasar al siguiente estado
                break;

            case 2: // Estado 3: LED rojo encendido
                GPIOB->ODR &= ~GPIO_ODR_OD8; // Apagamos el LED verde (pin 8)
                GPIOB->ODR &= ~GPIO_ODR_OD9; // Apagamos el LED amarillo (pin 9)
                GPIOB->ODR |= GPIO_ODR_OD6; // Encendemos el LED rojo (pin 6)
                estado_actual = 0; // Reiniciamos el estado actual a 0 para volver al primer estado
                break;


            default: // Estado 4: Todos los LEDs apagados
                estado_actual = 0; // Reiniciamos el estado actual a 0 para volver al primer estado
                break;
           
        
        }

        


    }


   }
}
