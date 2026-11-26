/**
 ******************************************************************************
 * @file           : mainJoystick.c
 * @author         : Juan Andrés Carreño Londoño / Miguel Ángel Bedoya González
 * @brief          : Joystick analógico leído con el ADC1 (12 bits, modo scan + interrupción)
 *                   y enviado por UART, con librerías HAL y organizado como una
 *                   Máquina de Estados Finitos (FSM)
 ******************************************************************************
 *
 *** Mapeo de Pines:
 ** JOYSTICK (ADC1, modo analógico):
 * - VRx -> PA0 (A0, ADC1_IN0)
 * - VRy -> PA1 (A1, ADC1_IN1)
 *
 ** UART USART2 - AF07 (llega al PC por el COM virtual del ST-Link):
 * - TX -> PA2
 * - RX -> PA3
 *
 ** BLINKY TIM4 -> PH1 (cambia cada 250 ms, siempre)
 *
 *
 * ===================== FUNCIONAMIENTO ===================== *
 *
 * - Reloj del sistema: HSI 16 MHz (sin PLL). APB1 y APB2 a 16 MHz.
 *
 * - TIM3 interrumpe cada 100 ms: es el periodo de muestreo del joystick.
 *
 * - ADC1 de 12 bits en modo scan: una sola orden convierte la secuencia
 *   rango 1 = canal 0 (eje X) y rango 2 = canal 1 (eje Y). Con EOC_SINGLE_CONV la
 *   interrupción llega al terminar CADA canal, y el callback guarda cada resultado en
 *   adc_raw[]. Reloj del ADC = 16 MHz / 2 = 8 MHz. Cada canal tarda 480 ciclos de muestreo
 *   + 12 ciclos de aproximaciones sucesivas (uno por bit) = 492 ciclos = 61,5 us.
 *
 * - Conversión: raw va de 0 a 4095 (2^12 - 1). mV = raw x 3300 / 4095.
 *
 * - USART2 a 115200 baudios, 8N1. Cada muestra se envía como una línea de texto:
 *   "X=2048 (1650 mV)  Y=3990 (3215 mV)  ABAJO\r\n"
 *
 * - TIM4 interrumpe cada 250 ms, sin importar lo demás. El LED PH1 cambia en cada interrupción.
 *
 *
 * ===================== FSM ===================== *
 *
 * Patrón Switch-Case. La FSM decide QUÉ pasa (política), las funciones driver hacen el
 * CÓMO (hardware). Los callbacks de la HAL solo levantan banderas volatile,
 * get_next_event() las convierte en un EVENTO y el while(1) nunca se bloquea esperando el ADC.
 *
 *   Estado actual     Evento              Estado siguiente   Acción [ENTER]
 *   ----------------  ------------------  -----------------  -----------------------------------
 *   STATE_IDLE        EV_ADC_DONE         STATE_CONVERT      -
 *   STATE_IDLE        EV_SAMPLE_TICK      STATE_START_ADC    -
 *   STATE_IDLE        EV_BLINK_TICK       STATE_BLINK        -
 *   STATE_START_ADC   (incondicional)     STATE_IDLE         adc_StartScan()
 *   STATE_CONVERT     (incondicional)     STATE_SEND_UART    dirección (los mV vienen del callback)
 *   STATE_SEND_UART   (incondicional)     STATE_IDLE         uart_SendSample()
 *   STATE_BLINK       (incondicional)     STATE_IDLE         led_Toggle()
 *   STATE_ERROR       (cualquiera)        STATE_IDLE         recuperación
 *
 ******************************************************************************
 */

//Includes

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdio.h>

//Estados de la FSM (el QUÉ)
typedef enum {
	STATE_IDLE, //Reposo: espera un evento sin hacer nada más (no bloquea)
	STATE_START_ADC, //Ordena al ADC convertir la secuencia X, Y (no espera el resultado)
	STATE_CONVERT, //Pasa los valores raw a milivoltios y calcula la dirección
	STATE_SEND_UART, //Arma la línea de texto y la envía por la USART2
	STATE_BLINK, //Atiende el tick del TIM4: cambia el estado del LED PH1
	STATE_ERROR, //Estado inválido (trampa de hardware): se recupera hacia STATE_IDLE
	NUM_STATES //Centinela: cantidad de estados, sirve para validar el rango
} State_t;

//Eventos de la FSM (según qué cambian los estados)
typedef enum {
	EV_NONE, //No ha ocurrido nada
	EV_ADC_DONE, //El ADC terminó los dos canales de la secuencia
	EV_SAMPLE_TICK, //Venció el periodo de muestreo (100 ms)
	EV_BLINK_TICK //Venció el periodo del blinky (250 ms)
} Event_t;

//Direcciones del joystick
typedef enum {
	DIR_CENTRO,
	DIR_ARRIBA,
	DIR_ABAJO,
	DIR_IZQUIERDA,
	DIR_DERECHA
} Dir_t;

//Handles de la HAL: guardan la configuración y el estado de cada periférico.
//Son globales porque los vectores de stm32f4xx_it.c también los usan
ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

//Variables
State_t current_state = STATE_IDLE; //Estado actual de la FSM, siempre inicia en reposo
uint8_t state_entry_flag = 1; //Vale 1 al llegar a un estado para que su acción [ENTER] corra exactamente una vez

volatile uint8_t sample_flag = 0; //Bandera que deja el callback del TIM3 (volatile: cambia fuera del flujo normal)
volatile uint8_t blink_flag = 0; //Bandera que deja el callback del TIM4
volatile uint8_t adc_done_flag = 0; //Bandera que deja el callback del ADC al terminar los dos canales
volatile uint16_t adc_raw[2] = {0, 0}; //Resultado de 12 bits de cada canal: [0] = X, [1] = Y
volatile uint8_t adc_index = 0; //Qué canal de la secuencia llega en el próximo callback

volatile uint32_t adc_mv[2] = {0, 0}; //Voltaje de cada canal en milivoltios: [0] = X, [1] = Y
Dir_t direction = DIR_CENTRO; //Dirección calculada a partir de los dos ejes

const char *dir_names[] = {"CENTRO", "ARRIBA", "ABAJO", "IZQUIERDA", "DERECHA"};

//Cabecera de funciones

//Drivers de la FSM (el CÓMO)
Event_t get_next_event(void); //Lee (y limpia) las banderas de los callbacks y las convierte en un evento
void adc_StartScan(void); //Arranca la secuencia X, Y en modo interrupción
Dir_t get_direction(uint16_t x, uint16_t y); //Clasifica la posición con una zona muerta alrededor del centro
void uart_SendSample(void); //Arma la línea de texto y la envía por la USART2
void led_Toggle(void); //Cambia el estado del LED PH1

//Inicialización del hardware
void SystemClock_Config(void);
void init_GPIO(void);
void init_ADC1(void);
void init_USART2(void);
void init_timer3(void);
void init_timer4(void);

//Main

int main(void){
	Event_t current_event = EV_NONE; //Evento que se evalúa en cada pasada del ciclo infinito

	HAL_Init(); 
	SystemClock_Config();
	init_GPIO();
	init_ADC1();
	init_USART2();
	init_timer3();
	init_timer4(); //Último: las interrupciones empiezan con todo el hardware listo

	/* El ciclo infinito nunca termina: lee un evento, evalúa el estado y reacciona.
	 * Regla de NO bloqueo: no hay HAL_Delay(), cada pasada procesa a lo sumo UN evento y UNA transición. */
	while(1){

		//Safeguard: valida el estado antes de usarlo, si está fuera de rango se recupera con STATE_ERROR
		if (current_state >= NUM_STATES) {
			current_state = STATE_ERROR;
			state_entry_flag = 1;
		}

		switch (current_state) {

		case STATE_IDLE: //Estado de reposo
			//[ENTER] Aquí no hay nada que preparar
			if (state_entry_flag) {
				state_entry_flag = 0;
			}

			//[DO] Solo IDLE consume eventos
			current_event = get_next_event();

			//Transiciones
			if (current_event == EV_ADC_DONE) {
				current_state = STATE_CONVERT;
				state_entry_flag = 1;
			}
			else if (current_event == EV_SAMPLE_TICK) {
				current_state = STATE_START_ADC;
				state_entry_flag = 1;
			}
			else if (current_event == EV_BLINK_TICK) {
				current_state = STATE_BLINK;
				state_entry_flag = 1;
			}
			break;

		case STATE_START_ADC: //Ordena la conversión
			//[ENTER] Arranca la secuencia; el resultado llega después por interrupción
			if (state_entry_flag) {
				adc_StartScan();
				state_entry_flag = 0;
			}
			//Transición incondicional: no se espera al ADC, se regresa al reposo
			current_state = STATE_IDLE;
			state_entry_flag = 1;
			break;

		case STATE_CONVERT: //Procesa los valores raw
			//[ENTER] Dirección del joystick (los mV ya los calculó el callback del ADC)
			if (state_entry_flag) {
				direction = get_direction(adc_raw[0], adc_raw[1]);
				state_entry_flag = 0;
			}
			//Transición incondicional: con los datos listos se pasa a enviarlos
			current_state = STATE_SEND_UART;
			state_entry_flag = 1;
			break;

		case STATE_SEND_UART: //Envía la muestra
			//[ENTER] Una línea de texto por cada muestra
			if (state_entry_flag) {
				uart_SendSample();
				state_entry_flag = 0;
			}
			current_state = STATE_IDLE;
			state_entry_flag = 1;
			break;

		case STATE_BLINK: //Estado que atiende el tick del blinky
			//[ENTER] Cambia el LED, exactamente una vez por cada evento del blinky
			if (state_entry_flag) {
				led_Toggle();
				state_entry_flag = 0;
			}
			current_state = STATE_IDLE;
			state_entry_flag = 1;
			break;

		case STATE_ERROR: //STATE_ERROR comparte código con default porque la recuperación es la misma
		default: //Trampa: cualquier estado desconocido se recupera de forma segura hacia el reposo
			current_state = STATE_IDLE;
			state_entry_flag = 1;
			break;
		}
	}
	return 0;
}

/* Funciones driver de la FSM (el CÓMO) */

//Convierte las banderas de los callbacks en UN evento. Prioridad: ADC > muestreo > blinky
Event_t get_next_event(void){
	if (adc_done_flag == 1) { //El ADC terminó la secuencia: hay datos nuevos
		adc_done_flag = 0; //Se baja la bandera: el evento se atiende una sola vez
		return EV_ADC_DONE;
	}
	if (sample_flag == 1) { //El TIM3 pide una muestra nueva
		sample_flag = 0;
		return EV_SAMPLE_TICK;
	}
	if (blink_flag == 1) { //El TIM4 pide cambiar el LED
		blink_flag = 0;
		return EV_BLINK_TICK;
	}
	return EV_NONE;
}

//Arranca la secuencia X, Y. Cada canal termina con una interrupción (ver HAL_ADC_ConvCpltCallback)
void adc_StartScan(void){
	adc_index = 0; //El primer resultado que llegue es el del rango 1 (eje X)
	HAL_ADC_Start_IT(&hadc1); //Equivale a ADC1->CR2 |= ADC_CR2_SWSTART, con EOCIE habilitado
}

//Zona muerta de 400 cuentas alrededor del centro (2048). Gana el eje que más se aleja
Dir_t get_direction(uint16_t x, uint16_t y){
	int32_t dx = (int32_t) x - 2048; //Con signo: negativo a un lado del centro, positivo al otro
	int32_t dy = (int32_t) y - 2048;
	int32_t ax; //Valor absoluto de dx (qué tan lejos del centro está X)
	int32_t ay; //Valor absoluto de dy

	if (dx < 0) {
		ax = -dx;
	}
	else {
		ax = dx;
	}

	if (dy < 0) {
		ay = -dy;
	}
	else {
		ay = dy;
	}

	if ((ax < 400) && (ay < 400)) { //Los dos ejes dentro de la zona muerta
		return DIR_CENTRO;
	}
	else if (ax >= ay) { //Gana el eje X
		if (dx > 0) {
			return DIR_DERECHA;
		}
		else {
			return DIR_IZQUIERDA;
		}
	}
	else { //Gana el eje Y. En este montaje el voltaje de Y sube hacia abajo
		if (dy > 0) {
			return DIR_ABAJO;
		}
		else {
			return DIR_ARRIBA;
		}
	}
}

//Arma la línea y la envía. HAL_UART_Transmit espera a que salga el último byte (unos 4 ms)
void uart_SendSample(void){
	char line[64];
	int len = sprintf(line, "X=%04u (%4lu mV)  Y=%04u (%4lu mV)  %s\r\n", //line[64] alcanza: la línea más larga tiene 47 caracteres + '\0'
	                   (unsigned) adc_raw[0], (unsigned long) adc_mv[0],
	                   (unsigned) adc_raw[1], (unsigned long) adc_mv[1],
	                   dir_names[direction]);
	HAL_UART_Transmit(&huart2, (uint8_t *) line, (uint16_t) len, 100); //Timeout de 100 ms
}

//Cambia el estado del LED PH1
void led_Toggle(void){
	HAL_GPIO_TogglePin(GPIOH, GPIO_PIN_1);
}

/* Funciones de inicialización del hardware */

//HSI de 16 MHz como reloj del sistema, sin PLL, todos los buses a 16 MHz
void SystemClock_Config(void){
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clk = {0};

	osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	osc.HSIState = RCC_HSI_ON;
	osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	osc.PLL.PLLState = RCC_PLL_NONE;
	HAL_RCC_OscConfig(&osc);

	clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	clk.AHBCLKDivider = RCC_SYSCLK_DIV1; //HCLK = 16 MHz
	clk.APB1CLKDivider = RCC_HCLK_DIV1; //APB1 = 16 MHz (TIM3, TIM4, USART2)
	clk.APB2CLKDivider = RCC_HCLK_DIV1; //APB2 = 16 MHz (ADC1)
	HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0); //0 estados de espera: correcto para 16 MHz
}

void init_GPIO(void){
	GPIO_InitTypeDef gpio = {0};

	/*
	PH1 (Blinky)
	*/
	__HAL_RCC_GPIOH_CLK_ENABLE(); //Igual a RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN
	gpio.Pin = GPIO_PIN_1;
	gpio.Mode = GPIO_MODE_OUTPUT_PP; //Salida push-pull
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOH, &gpio);
	HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET); //Inicia encendido

	/*
	Joystick (PA0 = VRx, PA1 = VRy) en modo analógico (MODER = 11)
	*/
	__HAL_RCC_GPIOA_CLK_ENABLE();
	gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
	gpio.Mode = GPIO_MODE_ANALOG; //Desconecta la etapa digital: el pin va directo al ADC
	gpio.Pull = GPIO_NOPULL; //Un pull-up alteraría el voltaje que se mide
	HAL_GPIO_Init(GPIOA, &gpio);

	/*
	USART2 (PA2 = TX, PA3 = RX) en función alternativa AF07
	*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Pull = GPIO_PULLUP; //La línea UART en reposo está en 1
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &gpio);
}

void init_ADC1(void){
	ADC_ChannelConfTypeDef ch = {0};

	__HAL_RCC_ADC1_CLK_ENABLE(); //Reloj del ADC1 (bus APB2)

	hadc1.Instance = ADC1;
	hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2; //16 MHz / 2 = 8 MHz (máximo 36 MHz)
	hadc1.Init.Resolution = ADC_RESOLUTION_12B; //12 bits: 4096 niveles, 12 ciclos de conversión
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT; //El resultado ocupa los bits 11..0 del DR
	hadc1.Init.ScanConvMode = ENABLE; //Convierte la secuencia completa (rango 1, rango 2)
	hadc1.Init.ContinuousConvMode = DISABLE; //Una secuencia por cada orden de la FSM
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START; //La orden la da el software (SWSTART)
	hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc1.Init.NbrOfConversion = 2; //Dos canales en la secuencia
	hadc1.Init.DMAContinuousRequests = DISABLE;
	hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV; //Interrupción al final de CADA canal, no solo de la secuencia
	HAL_ADC_Init(&hadc1);

	ch.Channel = ADC_CHANNEL_0; //PA0 = eje X
	ch.Rank = 1; //Primero en la secuencia
	ch.SamplingTime = ADC_SAMPLETIME_480CYCLES; //480 ciclos: da tiempo al callback de leer X antes de que llegue Y (si no, overrun)
	HAL_ADC_ConfigChannel(&hadc1, &ch);

	ch.Channel = ADC_CHANNEL_1; //PA1 = eje Y
	ch.Rank = 2; //Segundo en la secuencia
	HAL_ADC_ConfigChannel(&hadc1, &ch);

	HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(ADC_IRQn); //Habilita la línea del ADC en el NVIC
}

void init_USART2(void){
	__HAL_RCC_USART2_CLK_ENABLE(); //Reloj de la USART2 (bus APB1)

	huart2.Instance = USART2;
	huart2.Init.BaudRate = 115200; //115200 bits por segundo
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 bits de datos
	huart2.Init.StopBits = UART_STOPBITS_1; //1 bit de parada
	huart2.Init.Parity = UART_PARITY_NONE; //Sin paridad -> 8N1
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	HAL_UART_Init(&huart2);
}

//TIM3: evento de actualización cada 100 ms (periodo de muestreo)
void init_timer3(void){
	__HAL_RCC_TIM3_CLK_ENABLE();

	htim3.Instance = TIM3;
	htim3.Init.Prescaler = 16000 - 1; //16 MHz / 16000 = 1 kHz -> tick de 1 ms
	htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim3.Init.Period = 1000 - 1; //100 ms
	htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	HAL_TIM_Base_Init(&htim3);

	HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(TIM3_IRQn);
	HAL_TIM_Base_Start_IT(&htim3); //Habilita UIE y arranca el contador (CEN)
}

//TIM4: evento de actualización cada 250 ms (blinky)
void init_timer4(void){
	__HAL_RCC_TIM4_CLK_ENABLE();

	htim4.Instance = TIM4;
	htim4.Init.Prescaler = 16000 - 1; //Tick de 1 ms
	htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim4.Init.Period = 250 - 1; //250 ms
	htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	HAL_TIM_Base_Init(&htim4);

	HAL_NVIC_SetPriority(TIM4_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(TIM4_IRQn);
	HAL_TIM_Base_Start_IT(&htim4);
}

/* Callbacks de la HAL (solo registran el evento y regresan, sin lógica de la FSM) */

//La HAL la llama al terminar CADA canal de la secuencia (EOC_SINGLE_CONV)
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
	if (hadc->Instance == ADC1) {
		adc_raw[adc_index] = (uint16_t) HAL_ADC_GetValue(hadc); //Leer el DR también baja la bandera EOC
		adc_mv[adc_index] = ((uint32_t) adc_raw[adc_index] * 3300) / 4095; //0..4095 -> 0..3300 mV: se multiplica antes de dividir para no perder precisión
		adc_index++;
		if (adc_index >= 2) { //Llegaron X y Y: la FSM lo convierte en EV_ADC_DONE
			adc_done_flag = 1;
		}
	}
}

//Compartida por todos los timers: siempre revisar htim->Instance
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
	if (htim->Instance == TIM3) {
		sample_flag = 1; //La FSM lo convierte en EV_SAMPLE_TICK
	}
	else if (htim->Instance == TIM4) {
		blink_flag = 1; //La FSM lo convierte en EV_BLINK_TICK
	}
}
