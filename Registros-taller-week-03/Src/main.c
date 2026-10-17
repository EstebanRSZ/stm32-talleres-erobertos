/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Esteban Roberto Saiz , erobertos@unal.edu.co
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

#include <stm32f4xx.h>
#include <stdint.h>


/*----main-----*/
int main(void)
{
    /*Habilitar el reloj del GPIOA*/
    RCC->AHB1ENR |= (1<<0); // Habilitar el reloj del GPIOA (Bit 0 del registro AHB1ENR)
    
    /*Configurar el GPIOA como salida*/
    GPIOA->MODER &= ~(0b11<< (5*2)); // Limpiar los bits 5 y 4 del registro MODER(por eso se poner 11 porque es para borrar 2 "espacios"), deben ser 01 para salida y el *2 es para modificar 2 bits del registro MODER
    GPIOA->MODER |= (0b01<< (5*2)); // Configurar el pin 5 del GPIOA como salida (Bit 5 del registro MODER)

    /*Ahora vamos a configurar el tipo de salida como push-pull (OTYPER =0)*/
    GPIOA->OTYPER &= ~(1<<5); // Limpiar el bit 5 del registro OTYPER, para que sea de tipo push-pull (0), en este caso solo es 1 bit porque el registro OTYPER es de 1 bit por pin.

    /*Vamos a asignarle la velocidad Speed, en este caso low (00)*/
    GPIOA->OSPEEDR &= ~(0b11<< (5*2)); // Limpiar los bits 5 y 4 del registro OSPEEDR, para que sea de velocidad low (00)

    /*Vamos a asignarle el modo de operación, en este caso: No pull-up, pull-down (PUPDR = 00), sin resistencias internas*/
    GPIOA->PUPDR &= ~(0b11<<(5*2)); // Limpiar los bits 5 y 4 del registro PUPDR, para que sea de modo de operación sin resistencias internas (00)

    /*Configurar el ODR en 1, para encender el LED*/
    GPIOA->ODR &= ~(1<<5); // Asi nos aseguramos que el LED este apagado al inicio con el AND, entonces estamos limpiando la posicion del registro del ODR, ya que el ODR es de 1 bit por pin, y el bit 5 corresponde al pin 5 del GPIOA.
    GPIOA->ODR |= (1<<5); // Encender el LED, con el OR, ya que el ODR es de 1 bit por pin, y el bit 5 corresponde al pin 5 del GPIOA.

    while (1) {
    /* No metemos la configuracion del LED en el bucle, ya que eso se realiza sola 1 vez y no es necesario repetirlo indefinidamente*/
    }
}