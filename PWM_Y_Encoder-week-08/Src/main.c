/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Esteban Roberto Saiz, erobertos@unal.edu.co
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

//Includes
#include "stm32f411xe.h"
#include <stdint.h> //Tipos de ancho fijo (uint8_t, int32_t...) para que el tamaño de cada variable sea conocido
#include <stm32f4xx.h> 



/* Constantes */
#define COUNTS_PER_STEP	4 //El modo TI12 cuenta 4 flancos por cada paso mecánico del encoder
#define ARR_INIT		1999 //ARR inicial: (1999 + 1) x 1 us = 2000 us -> 500 Hz
#define CCR_INIT		0 //CCR1 inicial = 0
#define ARR_STEP		20 //Lo que suma o resta al ARR cada paso del encoder en MODE_ARR
#define ARR_MIN			99 //ARR mínimo: (99 + 1) x 1 us = 100 us -> 10 kHz (con ARR = 0 el contador queda bloqueado)
#define ARR_MAX			65534 //ARR máximo: periodo de unos 65 ms. TIM3 es de 16 bit y ARR + 1 (el CCR1 máximo) debe caber en 16 bit: con 65535 daría 65536, que se trunca a 0
#define CCR_STEP		10 //Lo que suma o resta al CCR1 cada paso del encoder en MODE_CCR
#define BLINK_CCR_MS	250 //PH1 cambia cada 250 ms en MODE_CCR
#define BLINK_ARR_MS	500 //PH1 cambia cada 500 ms en MODE_ARR

//MODOS DE EDICIÓN: qué valor del PWM modifica el encoder
typedef enum { //Lista de modos posibles
	MODE_CCR, //El encoder cambia el CCR1 (ancho del pulso), blinky cada 250 ms, cambia el dutyclycle
	MODE_ARR //El encoder cambia el ARR (periodo), blinky cada 500 ms, cambia la frecuencia del Blinky
} Mode_t; //Nombre del tipo: edit_mode solo puede recibir valores de este enum

//ESTADOS: la condición en la que está el sistema en un instante dado
typedef enum { //Lista de estados posibles, cada nombre vale un entero (0, 1, 2...)
	STATE_IDLE, //Reposo: espera un evento sin hacer nada más (no bloquea) ---> Todos parten de este estado de reposo, entonces se "obliga" a que el sistema vuelva a este estado luego de atender un evento
	STATE_UPDATE_PWM, //Atiende el giro del encoder: suma o resta al ARR o al CCR1 del PWM según el modo
	STATE_CHANGE_MODE, //Atiende el pulsador: cambia entre editar el CCR y editar el ARR
	STATE_BLINK, //Atiende el tick del TIM4: cambia el estado del LED PH1
	STATE_ERROR, //Estado inválido (trampa de hardware): se recupera hacia STATE_IDLE
	NUM_STATES //Centinela: cantidad de estados, sirve para validar el rango
} State_t; //Nombre del tipo: current_state solo puede recibir valores de este enum

//EVENTOS: los disparadores (ISR o lecturas) que provocan transiciones entre estados
typedef enum { //Lista de eventos posibles, cada nombre vale un entero (0, 1, 2...)
	EV_NONE, //No ha ocurrido nada
	EV_ENCODER_CHANGED, //El encoder avanzó o retrocedió al menos un paso
	EV_BUTTON_PRESS, //El pulsador del KY040 fue presionado (flanco de bajada)
	EV_BLINK_TICK //Venció el medio periodo del blinky (250 ms en MODE_CCR, 500 ms en MODE_ARR)
} Event_t; //Nombre del tipo: current_event solo puede recibir valores de este enum




//Inicialización del hardware
static void gpio_Init(void); //GPIOH PH1 como salida (blinky)
static void tim4_Init(void); //TIM4 con interrupción cada 250 ms o 500 ms (tiempo del blinky)
static void tim3_pwm_Init(void); //GPIOA PA6 + TIM3 CH1 en modo PWM 1
static void tim2_encoder_Init(void); //GPIOA PA0, PA1 + TIM2 en modo encoder
static void exti0_Init(void); //GPIOB PB0 + EXTI0 (flanco de bajada)


int main(void){

	Event_t current_event = EV_NONE; //Evento que se evalúa en cada pasada del ciclo infinito

	gpio_Init(); //Configura PH1 (blinky)
	tim3_pwm_Init(); //Configura el PWM en PA6
	tim2_encoder_Init(); //Configura el encoder
	exti0_Init(); //Configura el pulsador por EXTI0 en PB0
	tim4_Init(); //Último: las interrupciones del blinky empiezan con todo el hardware listo



    while(1){

        	//Safeguard: valida el estado antes de usarlo, si está fuera de rango se recupera con STATE_ERROR
		if (current_state >= NUM_STATES) //Un valor fuera de rango sólo pasa por corrupción de memoria
		{
			current_state = STATE_ERROR; //Se fuerza el estado de error para recuperarse de forma segura
			state_entry_flag = 1; //El estado de error también ejecuta su [ENTER]
		}

        switch (current_state) //Despacha al código del estado actual
		{

		case STATE_IDLE: //Estado de reposo
			//[ENTER] Corre una sola vez al llegar al estado (aquí no hay nada que preparar)
			if (state_entry_flag) //Solo es 1 en la primera pasada después de llegar al estado
			{
				state_entry_flag = 0; //Se baja para que [ENTER] no vuelva a ejecutarse mientras se siga en este estado
			}

			//[DO] Corre libremente en cada pasada: en reposo solo consulta (sin bloquear) si hay un evento pendiente
			current_event = get_next_event(); //Solo IDLE consume eventos: los otros estados duran una pasada y no lo necesitan

			//Transiciones: cada evento lleva a un estado distinto
			if (current_event == EV_ENCODER_CHANGED) //Se giró el encoder
			{
				current_state = STATE_UPDATE_PWM; //[EXIT] No hay acción de salida en este estado
				state_entry_flag = 1; //Se marca para que el estado nuevo ejecute su [ENTER]
			}
			else if (current_event == EV_BUTTON_PRESS) //Se presionó el pulsador
			{
				current_state = STATE_CHANGE_MODE; //Pasa a atender el cambio de modo
				state_entry_flag = 1; //Se marca para que el estado nuevo ejecute su [ENTER]
			}
			else if (current_event == EV_BLINK_TICK) //Venció el medio periodo del blinky
			{
				current_state = STATE_BLINK; //Pasa a cambiar el LED
				state_entry_flag = 1; //Se marca para que el estado nuevo ejecute su [ENTER]
			}
			break; //Sale del switch, la pasada termina y el while(1) vuelve a empezar

		case STATE_UPDATE_PWM: //Estado que atiende el giro del encoder
			//[ENTER] Acción de entrada (Moore): cambia el ARR o el CCR1, exactamente una vez por cada evento del encoder
			if (state_entry_flag) //Solo la primera pasada después de llegar a este estado
			{
				pwm_ApplySteps(encoder_TakeSteps()); //Toma los pasos del encoder y los aplica al valor que indica edit_mode
				state_entry_flag = 0; //Baja la bandera: la acción ya se ejecutó
			}
			//Transición incondicional: la tarea ya fue atendida, se regresa al reposo
			current_state = STATE_IDLE; //Vuelve a esperar eventos
			state_entry_flag = 1; //IDLE ejecutará su [ENTER]
			break; //Fin del estado

		case STATE_CHANGE_MODE: //Estado que atiende el pulsador
			//[ENTER] Acción de entrada (Moore): cambia de modo, exactamente una vez por cada pulsación
			if (state_entry_flag) //Solo la primera pasada después de llegar a este estado
			{
				mode_Toggle(); //MODE_CCR <-> MODE_ARR y tiempo del blinky (250 ms o 500 ms)
				state_entry_flag = 0; //Baja la bandera: la acción ya se ejecutó
			}
			//Transición incondicional: la tarea ya fue atendida, se regresa al reposo
			current_state = STATE_IDLE; //Vuelve a esperar eventos
			state_entry_flag = 1; //IDLE ejecutará su [ENTER]
			break; //Fin del estado

		case STATE_BLINK: //Estado que atiende el tick del blinky
			//[ENTER] Acción de entrada (Moore): cambia el LED, exactamente una vez por cada evento del blinky
			if (state_entry_flag) //Solo la primera pasada después de llegar a este estado
			{
				led_Toggle(); //Invierte el estado de PH1
				state_entry_flag = 0; //Baja la bandera: la acción ya se ejecutó
			}
			//Transición incondicional: la tarea ya fue atendida, se regresa al reposo
			current_state = STATE_IDLE; //Vuelve a esperar eventos
			state_entry_flag = 1; //IDLE ejecutará su [ENTER]
			break; //Fin del estado

		case STATE_ERROR: //STATE_ERROR comparte código con default porque la recuperación es la misma
		default: //Trampa (default:): cualquier estado desconocido se recupera de forma segura hacia el reposo
			current_state = STATE_IDLE; //Reinicia la FSM en un estado conocido
			state_entry_flag = 1; //IDLE ejecutará su [ENTER]
			break; //Fin del estado
		}
	}

	 return 0;
}


void gpio_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN; //Reloj del GPIOH
    GPIOH->MODER |= GPIO_MODER_MODER1_0; //01: salida
    GPIOH->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR1; //11: alta velocidad
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; //0: push-pull
    GPIOH->PUPDR &= ~GPIO_PUPDR_PUPDR1; //00: sin pull-up ni pull-down

}

static void tim4_Init(void)
{
	RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; //Reloj del TIM4

	TIM4->PSC = 15999; //16 MHz / 16000 = 1 kHz
	TIM4->ARR = BLINK_CCR_MS - 1; //Cuenta de 0 a 249: 250 ms (inicia en MODE_CCR)
	TIM4->CR1 |= TIM_CR1_ARPE; //ARR con preload: al cambiar de modo, el nuevo ARR entra al terminar el periodo actual

	TIM4->DIER |= TIM_DIER_UIE; //Habilita la interrupción por actualización (primer interruptor)
	__NVIC_EnableIRQ(TIM4_IRQn); //Habilita la interrupción en el NVIC (segundo interruptor)

	TIM4->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

/*Configuracion del Timer 3 en modo PWM para el PIN PA6 */
void tim3_pwm_Init(void){
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Reloj del GPIOA
    GPIOA->MODER &= ~(GPIO_MODER_MODER6); //Limpia los bits de moder
    GPIOA->MODER |= (GPIO_MODER_MODER6_1); //10: Función alternativa para el pin PA6 (TIM3_CH1)

    GPIOA->AFR[0] |= GPIO_AFRL_AFSEL6_1; //AF2 (los pines 0 a 7 están en AFR[0])

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; //Reloj del TIM3

    TIM3->PSC = 15; //Tick de 1 us
	TIM3->ARR = ARR_INIT; //Periodo: (1999 + 1) us
	TIM3->CCR1 = CCR_INIT; //Ancho del pulso
	TIM3->CR1 |= TIM_CR1_ARPE; //ARR con preload: el cambio de periodo no corta pulsos

    TIM3->CCMR1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2); //110: PWM modo 1, como ese registro tiene 3 posiciones desde 0 hasta 2 [2:0] entonces cada posicion seria: _2 _1 _0 , por tanto si queremos poner 110 ponemos en la funcion el _2 OR _1.
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE; //CCR1 con preload
    TIM3->CCER |= TIM_CCER_CC1E;

    TIM3->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
} 


/* Configuracion del Timer 2 para el encoder, y como el encoder tiene 3 patas CLK, DT y SW entonces van en pines diferentes y por tanto se necesitan configurar diferentes pines.

        AF01 (con pull-up):
        CLK -> PA0 (TIM2_CH1)
        DT -> PA1 (TIM2_CH2)

        SW -> PB0 (EXTI0) , es el pulsador del encoder.

    Modo encoder: 4 cuentas por paso.
*/
void tim2_encoder_Init(void){

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Reloj del GPIOA

    GPIOA->MODER |= (GPIO_MODER_MODER0_1 | GPIO_MODER_MODER1_1); //10: función alterna
    GPIOA->AFR[0] |= (GPIO_AFRL_AFRL0_0 | GPIO_AFRL_AFRL1_0); //AF01 en ambos pines PA0(TIM2_CH1, por eso AFRL0) y PA1(TIM2_CH2, por eso AFRL1) respectivamente 

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; //Reloj del TIM2

    TIM2->ARR = 0xFFFFFFFF; //ARR máximo: 32 bit, para que no se desborde el contador, es modo hexadecimal y por eso el maximo es FFFFFFFF, que es 2^32 - 1 = 4294967295 (de 32 bits como maximo)
    TIM2->CCMR1 |= (TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0); //01: CC1 y CC2 mapeados a TI1 y TI2. Recordemos que el CC es Capture/Compare, y el TI es Timer Input. Por tanto, CC1S = 01 significa que el canal de captura/compare 1 está mapeado al Timer Input 1 (TI1), y CC2S = 01 significa que el canal de captura/compare 2 está mapeado al Timer Input 2 (TI2). Esto permite que el TIM2 pueda capturar los flancos de señal del encoder en los pines PA0 y PA1 respectivamente ya que CC1S seria para el canal 1 y CC2S para el canal 2, y ambos canales estan mapeados a los pines del encoder.
    TIM2->CCMR1 |= (TIM_CCMR1_IC1F | TIM_CCMR1_IC2F); //Filtro máximo: rechaza el rebote del encoder
    TIM2->CCER |= (TIM_CCER_CC1E | TIM_CCER_CC2E); //Habilita la captura de los dos canales, es decir, permite que el TIM2 pueda capturar los flancos de señal del encoder en los pines PA0 y PA1 respectivamente .
    TIM2->SMCR |= (TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1); //011: modo encoder 3, cuenta los flancos de TI1 y TI2. EL modo 3 es el mas completo a diferencia del modo 1 y 2, ya que cuenta los flancos de ambos canales del encoder, lo que permite una mayor resolución en la medición de la posición del encoder. En este modo, el contador del TIM2 se incrementa o decrementa en función de los flancos de señal de ambos canales del encoder, lo que permite una medición más precisa de la posición del encoder.

    TIM2->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
       
}

static void exti0_Init(void) //Configura la interrupción externa EXTI0 para el pulsador del encoder (PB0), y ponemos static para que solo sea visible en este archivo main.c
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; //Reloj del GPIOB (PB0 queda como entrada, estado de reset)
	GPIOB->PUPDR |= GPIO_PUPDR_PUPDR0_0; //01: pull-up, en reposo lee 1 y al presionar baja a 0

	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; //Reloj del SYSCFG, el multiplexor de EXTI vive ahí
	SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI0_PB; //Configuramos la linea EXTI0 para que sea activada por el pin GPIOB0, La línea EXTI0 toma el pin 0 del puerto B

	EXTI->FTSR |= EXTI_FTSR_TR0; //Disparo por flanco de bajada
	EXTI->IMR |= EXTI_IMR_MR0; // Habilitamos la mascara de interrupcion de la linea EXTI0

	__NVIC_EnableIRQ(EXTI0_IRQn); //Habilita la interrupción en el NVIC 

}


/* Funciones driver de la FSM (el CÓMO) */

//Convierte las banderas de las ISR y el encoder en UN evento. Prioridad: pulsador > encoder > blinky
static Event_t get_next_event(void)
{
	if (button_flag == 1) //La ISR del pulsador dejó un evento pendiente
	{
		button_flag = 0; //Se baja la bandera: el evento se atiende una sola vez
		return EV_BUTTON_PRESS;
	}

	int32_t counts = (int32_t) TIM2->CNT; //Con signo: al girar en CCW desde 0, CNT pasa a 0xFFFFFFFF, que es -1
	if ((counts >= COUNTS_PER_STEP) || (counts <= -COUNTS_PER_STEP)) //Hay al menos un paso completo, CW o CCW
	{
		return EV_ENCODER_CHANGED;
	}

	if (blink_flag == 1) //La ISR del TIM4 dejó un evento pendiente
	{
		blink_flag = 0;
		return EV_BLINK_TICK;
	}

	return EV_NONE;
}


//Devuelve los pasos completos acumulados (+ CW, - CCW) y los descuenta del TIM2
static int32_t encoder_TakeSteps(void)
{
	int32_t steps = (int32_t) TIM2->CNT / COUNTS_PER_STEP; //División entera: 7 cuentas = 1 paso, -7 cuentas = -1 paso
	TIM2->CNT -= steps * COUNTS_PER_STEP; //Se descuentan solo las cuentas usadas, el residuo queda para el siguiente giro
	return steps;
}

//Suma los pasos al ARR o al CCR1 según el modo, y los limita
static void pwm_ApplySteps(int32_t steps)
{
	if (edit_mode == MODE_CCR)
	{
		ccr_value += steps * CCR_STEP; //Cambia el ancho del pulso
	}
	else
	{
		arr_value += steps * ARR_STEP; //Cambia el periodo
	}

	if (arr_value < ARR_MIN) arr_value = ARR_MIN; //El ARR se queda entre ARR_MIN y ARR_MAX
	if (arr_value > ARR_MAX) arr_value = ARR_MAX;
	if (ccr_value < 0) ccr_value = 0; //El CCR1 se queda entre 0 y ARR + 1
	if (ccr_value > arr_value + 1) ccr_value = arr_value + 1;

	TIM3->ARR = arr_value; //Con ARPE = 1 y OC1PE = 1 entran al terminar el periodo actual
	TIM3->CCR1 = ccr_value;
}

//Cambia el valor que edita el encoder y el tiempo del blinky (CCR: 250 ms, ARR: 500 ms)
static void mode_Toggle(void)
{
	if (edit_mode == MODE_CCR)
	{
		edit_mode = MODE_ARR;
		TIM4->ARR = BLINK_ARR_MS - 1; //El blinky pasa a 500 ms
	}
	else
	{
		edit_mode = MODE_CCR;
		TIM4->ARR = BLINK_CCR_MS - 1; //El blinky pasa a 250 ms
	}
}

//Cambia el estado del LED PH1
static void led_Toggle(void)
{
	GPIOH->ODR ^= GPIO_ODR_OD1; //XOR con 1: invierte el bit sin tocar los otros pines
}

/* Funciones ISR (solo registran el evento y regresan, sin lógica de la FSM) */

//Cada vez que vence el TIM4 (250 ms o 500 ms según el modo) deja la bandera del blinky
void TIM4_IRQHandler(void)
{
	TIM4->SR &= ~TIM_SR_UIF; //Baja la bandera (escribiendo 0) para que la interrupción se pueda repetir
	blink_flag = 1; //La FSM lo convierte en EV_BLINK_TICK
}

//Pulsador por EXTI0
void EXTI0_IRQHandler(void)
{
	EXTI->PR = EXTI_PR_PR0; //Se baja escribiendo 1 (en EXTI->PR se limpia con 1, no con 0)
	button_flag = 1; //La FSM lo convierte en EV_BUTTON_PRESS
}