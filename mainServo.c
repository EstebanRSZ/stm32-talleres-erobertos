/**
 ******************************************************************************
 * @file           : mainServo.c
 * @brief          : Encoder rotatorio (TIM2) mueve un servomotor con una señal PWM de 50 Hz (TIM3),
 *                   organizado como una Máquina de Estados Finitos (FSM)
 ******************************************************************************
 *
 *** Mapeo de Pines (los mismos de mainME.c):
 ** PWM TIM3 - AF02 (señal del servo):
 * - CH1 (PWM)  -> PA6
 *
 ** ENCODER TIM2 - AF01:
 * - CH1 (CLK) -> PA0
 * - CH2 (DT)  -> PA1
 *
 ** BLINKY TIM4 -> PH1 (cambia cada 250 ms, siempre)
 *
 *
 * ===================== FUNCIONAMIENTO ===================== *
 *
 * - TIM2 en modo encoder 3 cuenta los flancos de CLK y DT: cada paso físico del encoder
 *   son 4 cuentas en TIM2->CNT. Cada paso suma o resta una cantidad fija al ancho del pulso.
 *
 * - TIM3 genera la señal del servo en PA6. Con PSC = 15 cada tick dura 1 us:
 *   periodo = (ARR + 1) us. Con ARR = 19999 el periodo es de 20 ms (50 Hz), que es el que
 *   espera el servo, y NO cambia nunca. El ancho del pulso es CCR1 us y es lo que define
 *   el ángulo: 500 us -> 0°, 1500 us -> 90° (centro), 2500 us -> 180°.
 *   Arranca con CCR1 = 1500 (servo en el centro).
 *
 * - Cada paso del encoder suma o resta 40 us al CCR1 (SERVO_STEP). El CCR1 se satura entre
 *   500 us y 2500 us para no forzar el servo contra sus topes mecánicos.
 *   Ajuste los límites a su servo si el suyo usa otro rango (p. ej. 1000 a 2000 us).
 *
 * - TIM4 interrumpe cada 250 ms. El LED PH1 cambia en cada interrupción.
 *
 * - Reloj del sistema: HSI 16 MHz (sin PLL), los timers reciben 16 MHz.
 *
 * - Alimente el servo con 5 V externos (no del pin de 3V3) y una la tierra con la de la tarjeta.
 *
 *
 * ===================== FSM ===================== *
 *
 * Patrón Switch-Case. La FSM decide QUÉ pasa (política), las funciones driver hacen el
 * CÓMO (hardware). Las ISR solo levantan banderas volatile, get_next_event() las
 * convierte en un EVENTO y el while(1) nunca se bloquea.
 *
 * STATE_IDLE es el centro: es el único estado que lee eventos. Cada evento lo lleva a un
 * estado que hace UNA tarea y vuelve a IDLE.
 *
 *   Estado actual      Evento              Estado siguiente   Acción [ENTER]
 *   -----------------  ------------------  -----------------  -----------------------------------------
 *   STATE_IDLE         EV_ENCODER_CHANGED  STATE_UPDATE_SERVO -
 *   STATE_IDLE         EV_BLINK_TICK       STATE_BLINK        -
 *   STATE_UPDATE_SERVO (incondicional)     STATE_IDLE         servo_ApplySteps()
 *   STATE_BLINK        (incondicional)     STATE_IDLE         led_Toggle()
 *   STATE_ERROR        (cualquiera)        STATE_IDLE         recuperación
 *
 ******************************************************************************
 */

//Includes

#include "stm32f411xe.h"
#include <stdint.h>
#include <stm32f4xx.h>

//Constantes
#define COUNTS_PER_STEP	4 //El modo encoder 3 cuenta 4 flancos por cada paso mecánico del encoder
#define SERVO_ARR		19999 //ARR fijo: (19999 + 1) x 1 us = 20 ms -> 50 Hz
#define SERVO_CENTER	1500 //Pulso inicial de 1500 us: servo en el centro (90°)
#define SERVO_MIN		500 //Pulso mínimo de 500 us: 0°
#define SERVO_MAX		2500 //Pulso máximo de 2500 us: 180°
#define SERVO_STEP		40 //Lo que suma o resta al CCR1 cada paso del encoder (us)

//Estados de la FSM (el QUÉ)
typedef enum {
	STATE_IDLE, //Reposo: espera un evento sin hacer nada más (no bloquea)
	STATE_UPDATE_SERVO, //Atiende el giro del encoder: suma o resta al ancho del pulso (CCR1)
	STATE_BLINK, //Atiende el tick del TIM4: cambia el estado del LED PH1
	STATE_ERROR, //Estado inválido (trampa de hardware): se recupera hacia STATE_IDLE
	NUM_STATES //Centinela: cantidad de estados, sirve para validar el rango
} State_t;

//Eventos de la FSM (Según que cambian los estados)
typedef enum {
	EV_NONE, //No ha ocurrido nada
	EV_ENCODER_CHANGED, //El encoder avanzó o retrocedió al menos un paso
	EV_BLINK_TICK //Venció el periodo del blinky (250 ms)
} Event_t;

//Variables
State_t current_state = STATE_IDLE; //Estado actual de la FSM, siempre inicia en reposo
uint8_t state_entry_flag = 1; //Vale 1 al llegar a un estado para que su acción [ENTER] corra exactamente una vez

volatile uint8_t blink_flag = 0; //Bandera que deja la ISR del TIM4 (volatile: cambia fuera del flujo normal)

int32_t ccr_value = SERVO_CENTER; //CCR1 que tiene el TIM3 (ancho del pulso en ticks de 1 us). Con signo para comparar con los límites

//Cabecera de funciones

//Drivers de la FSM (el CÓMO)
Event_t get_next_event(void); //Lee (y limpia) la bandera de la ISR y la lectura del encoder, y las convierte en un evento
void servo_ApplySteps(void); //Lee los pasos del TIM2, los suma x SERVO_STEP al CCR1 (con saturación) y pone el CNT en 0
void led_Toggle(void); //Cambia el estado del LED PH1

//Inicialización del hardware
void init_GPIO(void);
void init_timer4(void);
void init_timer3PWM(void);
void init_tim2_encoder(void);

//Main

int main(void){
    Event_t current_event = EV_NONE; //Evento que se evalúa en cada pasada del ciclo infinito

    init_GPIO();
    init_timer3PWM();
    init_tim2_encoder();
    init_timer4(); //Último: las interrupciones del blinky empiezan con todo el hardware listo

    /* El ciclo infinito nunca termina: lee un evento, evalúa el estado y reacciona.
     * Regla de NO bloqueo: no hay delay(), cada pasada procesa a lo sumo UN evento y UNA transición. */
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

            //[DO] Solo IDLE consume eventos: los otros estados duran una pasada y no lo necesitan
            current_event = get_next_event();

            //Transiciones
            if (current_event == EV_ENCODER_CHANGED) {
                current_state = STATE_UPDATE_SERVO;
                state_entry_flag = 1;
            }
            else if (current_event == EV_BLINK_TICK) {
                current_state = STATE_BLINK;
                state_entry_flag = 1;
            }
            break;

        case STATE_UPDATE_SERVO: //Atiende el giro del encoder
            //[ENTER] Mueve el servo, exactamente una vez por cada evento del encoder
            if (state_entry_flag) {
                servo_ApplySteps();
                state_entry_flag = 0;
            }
            //Transición incondicional: la tarea ya fue atendida, se regresa al reposo
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

//Convierte la bandera de la ISR y el encoder en UN evento. Prioridad: encoder > blinky
Event_t get_next_event(void){
    int32_t counts = (int32_t) TIM2->CNT; //Con signo: al girar en CCW desde 0, CNT pasa a 0xFFFFFFFF, que es -1
    if ((counts >= COUNTS_PER_STEP) || (counts <= -COUNTS_PER_STEP)) { //Hay al menos un paso completo, CW o CCW
        return EV_ENCODER_CHANGED;
    }

    if (blink_flag == 1) { //La ISR del TIM4 dejó un evento pendiente
        blink_flag = 0; //Se baja la bandera: el evento se atiende una sola vez
        return EV_BLINK_TICK;
    }

    return EV_NONE;
}

//Convierte las cuentas del TIM2 en pasos, los suma al CCR1 y lo limita entre SERVO_MIN y SERVO_MAX
void servo_ApplySteps(void){
    int32_t steps = (int32_t) TIM2->CNT / COUNTS_PER_STEP; //División entera: 7 cuentas = 1 paso, -7 cuentas = -1 paso
    TIM2->CNT = 0; //Las cuentas ya se usaron: el contador vuelve a 0 (el residuo de un paso incompleto se descarta)

    ccr_value += steps * SERVO_STEP; //40 us por paso: cambia el ángulo del servo

    if (ccr_value < SERVO_MIN) ccr_value = SERVO_MIN;
    if (ccr_value > SERVO_MAX) ccr_value = SERVO_MAX;

    TIM3->CCR1 = ccr_value; //Con OC1PE = 1 entra al terminar el periodo actual (no corta pulsos)
}

//Cambia el estado del LED PH1
void led_Toggle(void){
    GPIOH->ODR ^= GPIO_ODR_OD1; //XOR con 1: invierte el bit sin tocar los otros pines
}

/* Funciones de inicialización del hardware */

void init_GPIO(void){

    /*
    PH1 (Blinky)
    */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN;   //Se enciende la señal de reloj
    GPIOH->MODER &= ~GPIO_MODER_MODE1;   //Se limpian los bits
    GPIOH->MODER |= GPIO_MODER_MODE1_0; //Se configura en (01) salida de proposito general
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; //Salida push-pull
    GPIOH->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR1; //Velocidad baja
    GPIOH->PUPDR &= ~GPIO_PUPDR_PUPD1; //No pull up/down
    GPIOH->ODR |= GPIO_ODR_OD1; //Inicia encendido

    /*
    PWM del servo (PA6 AF02 (TIM3 -> CH1))
    */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Señal de reloj puerto A
    GPIOA->MODER &= ~GPIO_MODER_MODE6; //Limpiamos (00)
    GPIOA->MODER |= GPIO_MODER_MODE6_1; //(10) Función alternativa
    GPIOA->AFR[0] &= ~GPIO_AFRL_AFRL6; //Limpiamos
    GPIOA->AFR[0] |= GPIO_AFRL_AFRL6_1; //(0010 AF02)

    /*
    Encoder (CLK -> PA0 AF01 (TIM2->CH1); DT -> PA1 AF01 (TIM2->CH2))
    */
    GPIOA->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1); //Limpiamos (00)
    GPIOA->MODER |= (GPIO_MODER_MODE0_1 | GPIO_MODER_MODE1_1); //(10) Función alternativa
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFRL0 | GPIO_AFRL_AFRL1); //Limpiamos
    GPIOA->AFR[0] |= (GPIO_AFRL_AFRL0_0 | GPIO_AFRL_AFRL1_0); //(0001 AF01 en ambos casos)
}

void init_timer4(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; //Reloj del TIM4

    TIM4->PSC = 16000-1; //16 MHz / 16000 = 1 kHz
    TIM4->ARR = 250-1; //250 ms
    TIM4->CR1 |= TIM_CR1_ARPE; //ARR con preload
    TIM4->EGR |= TIM_EGR_UG; //Fuerza una actualización para cargar PSC y ARR antes de arrancar
    TIM4->SR &= ~TIM_SR_UIF; //El UG levanta UIF: se baja para que no cuente como un tick falso
    TIM4->DIER |= TIM_DIER_UIE; //Habilita la interrupción por actualización (primer interruptor)
    __NVIC_EnableIRQ(TIM4_IRQn); //Habilita la interrupción en el NVIC (segundo interruptor)

    TIM4->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

void init_timer3PWM(void){

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; //Reloj del TIM3

    TIM3->PSC = 16-1; //Tick de 1 us
    TIM3->ARR = SERVO_ARR; //Periodo: (19999 + 1) us = 20 ms -> 50 Hz
    TIM3->CCR1 = ccr_value; //Ancho del pulso: 1500 us (servo al centro)
    TIM3->CR1 |= TIM_CR1_ARPE; //ARR con preload

    TIM3->CCMR1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2); //110: PWM modo 1
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE; //CCR1 con preload
    TIM3->CCER |= TIM_CCER_CC1E; //Habilita la salida del canal 1 al pin
    TIM3->EGR |= TIM_EGR_UG; //Fuerza una actualización para cargar PSC, ARR y CCR1 antes de arrancar

    TIM3->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

void init_tim2_encoder(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; //Reloj del TIM2

    TIM2->ARR = 0xFFFFFFFF; //TIM2 es de 32 bit: no se desborda en uso normal
    TIM2->CCMR1 |= (TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0); //01: CC1 y CC2 mapeados a TI1 y TI2
    TIM2->CCMR1 |= (TIM_CCMR1_IC1F | TIM_CCMR1_IC2F); //Filtro máximo: rechaza el rebote del encoder
    TIM2->CCER |= (TIM_CCER_CC1E | TIM_CCER_CC2E); //Habilita la captura de los dos canales

    TIM2->SMCR |= (TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1); //011: modo encoder 3, cuenta los flancos de TI1 y TI2
    TIM2->CR1 |= TIM_CR1_CEN; //Arranca el contador
}

/* Funciones ISR (solo registran el evento y regresan, sin lógica de la FSM) */

//Cada vez que vence el TIM4 (cada 250 ms) deja la bandera del blinky
void TIM4_IRQHandler(void)
{
    TIM4->SR &= ~TIM_SR_UIF; //Baja la bandera (escribiendo 0) para que la interrupción se pueda repetir
    blink_flag = 1; //La FSM lo convierte en EV_BLINK_TICK
}

