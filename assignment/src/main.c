#include "stdio.h"

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"

#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "led7seg.h"
#include "light.h"
#include "temp.h"

#define DEBUG_HEAT

#ifdef DEBUG_HEAT
#define DEBUG_HEAT_OFFSET 190
#endif
#ifndef DEBUG_HEAT
#define DEBUG_HEAT_OFFSET 0
#endif

#define SCREEN_CHG_DELAY 500
#define TEMP_HIGH_WARNING 450

/*** Message strings ***/
unsigned char* STR_CEMS_ALERT = "User %s has requested for assistance.\r\n";
unsigned char* STR_FIRE_ALERT = "Fire was Detected.\r\n";
unsigned char* STR_DARK_ALERT = "Movement in darkness was Detected.\r\n";
unsigned char* STR_MONITOR_MODE = "Entering MONITOR Mode.\r\n";

unsigned char* STR_ARROW_CHAR = ">";
unsigned char* STR_BLANK_CHAR = " ";

unsigned char* STR_INTVALUES_OUTPUT = "%d    ";
unsigned char* STR_UINTVALUES_OUTPUT = "%u    ";
unsigned char* STR_FLOATVALUES_OUTPUT = "%.2f   ";

unsigned char* STR_FUNC_TITLE = "Select Function:";
unsigned char* STR_FUNC_1 = "Siren       ";
unsigned char* STR_FUNC_2 = "SOS to CEMS ";
unsigned char* STR_FUNC_3 = "Lights      ";
unsigned char* STR_FUNC_4 = "$$$$$       ";

unsigned char* STR_MAIN_LUX = "LUX : ";
unsigned char* STR_MAIN_TEMP = "TEMP: ";
unsigned char* STR_MAIN_ACCX = "ACCX: ";
unsigned char* STR_MAIN_ACCY = "ACCY: ";
unsigned char* STR_MAIN_ACCZ = "ACCZ: ";
unsigned char* STR_MAIN_TITLE = "MODE: MONITOR";

unsigned char* STR_BIG_TEMP = "TEMP   ";
unsigned char* STR_BIG_LIGHT = "LUX   ";
unsigned char* STR_BIG_ACCX = "ACC X  ";
unsigned char* STR_BIG_ACCY = "ACC Y  ";
unsigned char* STR_BIG_ACCZ = "ACC Z  ";

/*** device/user id ***/
const char* userID = "EE2024";

/*** LED params ***/
static uint8_t rgbLED_mask = 0x00;
static uint8_t rgbLED_set = 0x03;
static volatile uint8_t rgbLED_flag = 0;

static uint32_t led_set = 0x0001; //for array
static volatile uint8_t led_array_flag = 0;
static uint8_t leds_toggle_flag = 0;

/*** timer params ***/
volatile uint32_t msTicks = 0; // counter for 1ms SysTicks
uint32_t oldSampleTicks = 0;
uint32_t oldSpeakerTicks = 0;

/*** 7-segment display params ***/
volatile uint8_t sseg_flag = 0;
unsigned int timer2count = 0;
int monitor_symbols[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A',
		'B', 'C', 'D', 'E', 'F' };

/*** Speaker params ***/
volatile uint8_t on_note = 0;
uint8_t speaker_on_flag = 0;

/*** ISL290003 light sensor params ***/
uint32_t light_reading = 0;
uint8_t movement_lowLight_flag = 0;
volatile uint8_t detect_darkness_flag = 1; // 0 also means that darkness is detected

/*** MMA7455 accelerometer sensor params ***/
int8_t accInitX, accInitY, accInitZ; //for offsetting
int8_t accX, accY, accZ;
int8_t accOldX, accOldY, accOldZ;
volatile uint8_t movement_detected_flag = 0;
volatile uint32_t lastMotionDetectedTicks = 0;

/*** temperature sensor ***/
int32_t temperature_reading = 0;
uint8_t temp_high_flag = 0;

/*** stable, monitor mode flag ***/
volatile uint8_t mode_flag = 0; //1 - monitor, 0 - passive

/*** flag to sample light and accelerometer sensors ***/
volatile uint8_t sample_sensors_flag = 0;

/*** OLED params ***/
volatile uint32_t lastScreenChangeTicks = 0;
volatile uint8_t oled_page_state = 0; //0 - default, 1 - temp, 2 - lux, 3 - accX, 4- accY, 5- accZ, 6 - funcMode
volatile uint8_t reinit_screen_flag = 0;
uint8_t tempStr[80];

/*** Function mode params ***/
volatile uint8_t func_mode_selection = 0; //0 - Siren, 1 - SOS to CEMS, 2 - Lights, 3 - $$$$$
volatile uint8_t func_change_flag = 0; //init to 1 to set 1st arrow
volatile uint8_t func_execute_flag = 0;

/*** UART params ***/
volatile uint8_t send_message_flag = 0;

/*** Rotary Switch params ***/
volatile uint8_t font_size = 2;
volatile uint8_t rotary_flag_0 = 0;
volatile uint8_t rotary_flag_1 = 0;

void rgbLED_controller(void);
void sseg_controller(void);
void prep_passiveMode();

/*** protocols initialisers ***/
static void init_GPIO(void) {
	PINSEL_CFG_Type PinCfg;

	/* Init Ext LED to GPIO PIO2_8 P2.8 */
	PinCfg.Pinnum = 2;
	PinCfg.Portnum = 2;
	PINSEL_ConfigPin(&PinCfg);
	GPIO_SetDir(2, (1 << 8), 1);

	/* Initialize SW4 pin connect to GPIO P2.11 (EINT1)*/
	PinCfg.Pinmode = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 11;
	PinCfg.Funcnum = 1;
	PINSEL_ConfigPin(&PinCfg);
	/* Initialize SW3 pin connect to GPIO P2.10 (EINT0) */
	PinCfg.Pinnum = 10;
	PINSEL_ConfigPin(&PinCfg);

	/* ---- Speaker ------> */
	GPIO_SetDir(0, 1 << 27, 1);
	GPIO_SetDir(0, 1 << 28, 1);
	GPIO_SetDir(2, 1 << 13, 1);

	GPIO_ClearValue(0, 1 << 27); //LM4811-clk
	GPIO_ClearValue(0, 1 << 28); //LM4811-up/dn
	GPIO_ClearValue(2, 1 << 13); //LM4811-shutdn
	/* <---- Speaker ------ */
}

//i2c enabler
static void init_I2C2(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect P0.10 */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C2 peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C2 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

//ssp enabler
static void init_SSP(void) {
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
//	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 6;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);
}

//uart3 pincfg
static void pinsel_uart3(void) {
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);
}

//uart enabler
static void init_uart(void) {
	UART_CFG_Type uartCfg;
	uartCfg.Baud_rate = 115200;
	uartCfg.Databits = UART_DATABIT_8;
	uartCfg.Parity = UART_PARITY_NONE;
	uartCfg.Stopbits = UART_STOPBIT_1;

	//init uart3
	pinsel_uart3();
	UART_Init(LPC_UART3, &uartCfg);
	UART_TxCmd(LPC_UART3, ENABLE);
}

//timer1 w/ 0.33s~ period
static void init_timer1() {

	// default value of PCLK = CCLK/4
	// CCLK is derived from PLL0 output signal / (CCLKSEL + 1) [CCLKCFG register]

	LPC_SC ->PCONP |= (1 << 2); /* Power ON Timer1 */

	LPC_TIM1 ->MCR = (1 << 0) | (1 << 1); /* Clear COUNT on MR0 match and Generate Interrupt */
	LPC_TIM1 ->PR = 0; /* Update COUNT every (value + 1) of PCLK  */
	LPC_TIM1 ->MR0 = 8888888; /* Value of COUNT that triggers interrupts */
	LPC_TIM1 ->TCR = (1 << 0); /* Start timer by setting the Counter Enable */

	NVIC_EnableIRQ(TIMER1_IRQn); /* Enable Timer1 interrupt */

}

//timer2 w/ 1s~ period
static void init_timer2() {

	// default value of PCLK = CCLK/4
	// CCLK is derived from PLL0 output signal / (CCLKSEL + 1) [CCLKCFG register]

	LPC_SC ->PCONP |= (1 << 22); /* Power ON Timer2 */

	LPC_TIM2 ->MCR = (1 << 0) | (1 << 1); /* Clear COUNT on MR0 match and Generate Interrupt */
	LPC_TIM2 ->PR = 0; /* Update COUNT every (value + 1) of PCLK  */
	LPC_TIM2 ->MR0 = 26666666; /* Value of COUNT that triggers interrupts */
	LPC_TIM2 ->TCR = (1 << 0); /* Start timer by setting the Counter Enable */

	NVIC_EnableIRQ(TIMER2_IRQn); /* Enable Timer2 interrupt */

}

void TIMER1_IRQHandler(void) {
	unsigned int isrMask;

	isrMask = LPC_TIM1 ->IR;
	LPC_TIM1 ->IR = isrMask; /* Clear the Interrupt Bit by writing to the register */// bitwise not

	rgbLED_flag = 1;

	//slower, delay but much less likely to crash
	if (rgbLED_flag
			&& ((((rgbLED_mask & RGB_BLUE) >> 1))
					|| ((rgbLED_mask & RGB_RED) >> 0))) {
		rgbLED_controller();
		rgbLED_flag = 0;
	}

	led_array_flag = 1;
}

void TIMER2_IRQHandler(void) {
	unsigned int isrMask;

	isrMask = LPC_TIM2 ->IR;
	LPC_TIM2 ->IR = isrMask; /* Clear the Interrupt Bit by writing to the register */// bitwise not

	if (timer2count == 5 || timer2count == 10 || timer2count == 15) {
		sample_sensors_flag = 1;
	}

	if (timer2count == 15) {
		send_message_flag = 1;
	}

	sseg_flag = 1;
}

/*** SysTick helper functions ***/
void SysTick_Handler(void) {
	msTicks++;
}

uint32_t getTicks(void) {
	return msTicks;
}

/** light_sensor helper functions **/\

//configure light sensors thresholds to detect any lighting
void lightSensor_detectLight() {
	light_setLoThreshold(0);
	light_setHiThreshold(50);
}

//configures light sensor's HI and LO thresholds to detect low light conditions
void lightSensor_detectDarkness() {
	light_setLoThreshold(50);
	light_setHiThreshold(972);
}

//protocol init
void init_protocols() {
	//protocol init
	init_I2C2();
	init_SSP();
	init_uart();
}

//sensors, peripherals init
void init_peripherals() {
	//GPIO devices init
	pca9532_init(); //port expander for led array
	rgb_init(); //rgb led
	temp_init(getTicks); //temperature sensor
	//SSP/GPIO devices init
	led7seg_init(); //seven-segment display
	oled_init(); //OLED display module
	//I2C sensors init
	light_init(); //light sensor module
	acc_init(); //accelerometer sensor
}

//interrupts init
void init_interrupts() {
	//interrupts init
	init_timer1(); //0.33s period clock
	init_timer2(); //1s period clock

	//light sensor
	LPC_GPIOINT ->IO2IntClr |= 1 << 5;
	LPC_GPIOINT ->IO2IntEnF |= 1 << 5; // enable light interrupt
	light_clearIrqStatus();
	//configure default light threshold
	lightSensor_detectDarkness();

	// rotary switch pio1_0 P0.24, pio1_1 P0.25
	LPC_GPIOINT ->IO0IntClr |= 1 << 24;
	LPC_GPIOINT ->IO0IntClr |= 1 << 25;
	LPC_GPIOINT ->IO0IntEnR |= 1 << 24;
	LPC_GPIOINT ->IO0IntEnR |= 1 << 25;

	// joystick interrupts
	LPC_GPIOINT ->IO0IntEnF |= 1 << 15;
	LPC_GPIOINT ->IO0IntEnF |= 1 << 16;
	LPC_GPIOINT ->IO0IntEnF |= 1 << 17;
	LPC_GPIOINT ->IO2IntEnF |= 1 << 3;
	LPC_GPIOINT ->IO2IntEnF |= 1 << 4;

	//configuring EINTx (0,1)
	int * EXT_INT_Mode_Register = (int *) 0x400fc148;
	*EXT_INT_Mode_Register |= 1 << 0; // edge sensitive
	*EXT_INT_Mode_Register |= 1 << 1;
	int * EXT_INT_Polarity_Register = (int *) 0x400fc14c;
	*EXT_INT_Polarity_Register |= 0 << 0; // falling edge
	*EXT_INT_Polarity_Register |= 0 << 1;

	NVIC_ClearPendingIRQ(EINT0_IRQn);
	NVIC_ClearPendingIRQ(EINT1_IRQn);
	NVIC_ClearPendingIRQ(EINT3_IRQn);

	NVIC_EnableIRQ(EINT0_IRQn);
	NVIC_EnableIRQ(EINT1_IRQn);
	NVIC_EnableIRQ(EINT3_IRQn);

}

//sets the sseg to the corresponding symbol
void sseg_controller(void) {
	led7seg_setChar(monitor_symbols[timer2count++], 0);
	timer2count %= 16;
}

//toggles LEDs
void rgbLED_controller(void) {
	rgbLED_set = ~rgbLED_set;
	rgb_setLeds(rgbLED_mask & rgbLED_set);
}

//sets the led arrays' leds
void ledArray_controller(void) {
	led_set = leds_toggle_flag ? 0xAAAA : 0x0000;

	pca9532_setLeds(led_set, 0xFFFF); //moves onLed down array
}

//controls the siren output by the piezo speaker (non-blocking)
void speaker_controller() {
	if (getTicks() > oldSpeakerTicks) {
		on_note = !on_note;

		oldSpeakerTicks = getTicks();

		if (on_note) {
			GPIO_SetValue(0, 1 << 26);
		} else {
			GPIO_ClearValue(0, 1 << 26);
		}
	}
}

//sets the Ext LED
void extLED_controller() {
	if (leds_toggle_flag) {
		GPIO_SetValue(2, (1 << 8));
	} else {
		GPIO_ClearValue(2, 1 << 8);
	}
}

void EINT0_IRQHandler(void) {
	if (oled_page_state == 6) {
		func_execute_flag = 1;
	}

	NVIC_ClearPendingIRQ(EINT0_IRQn);
	LPC_SC ->EXTINT = (1 << 0); /* Clear Interrupt Flag */

}

void EINT1_IRQHandler(void) {
	mode_flag = !mode_flag;

	NVIC_ClearPendingIRQ(EINT1_IRQn);
	LPC_SC ->EXTINT = (1 << 1); /* Clear Interrupt Flag */
}

uint8_t acw = 0;
uint8_t cw = 0;

void check_rotary_switch(void) {
	// GPIO interrupts on P0.24, P0.25
	// Check if ANY of the edges detected
	if ((LPC_GPIOINT ->IO0IntStatR >> 24) & 0x1) {
		rotary_flag_0 = 1;
		// channel 1 happened before channel 0
		// anti-clockwise
		if (rotary_flag_0 && rotary_flag_1) {

			acw++;

			if ((getTicks() > lastScreenChangeTicks + SCREEN_CHG_DELAY)
					&& mode_flag && (acw > 10)) {
				reinit_screen_flag = 1;
				oled_page_state = (oled_page_state == 0 ? 6 : oled_page_state - 1);

				acw = 0;
				lastScreenChangeTicks = getTicks();
			}

			rotary_flag_0 = 0;
			rotary_flag_1 = 0;
		}
		LPC_GPIOINT ->IO0IntClr = 1 << 24; //clear GPIO interrupt
	} else if (((LPC_GPIOINT ->IO0IntStatR >> 25) & 0x1)) {
		rotary_flag_1 = 1;
		// channel 0 happened before channel 1
		// clockwise
		if (rotary_flag_0 && rotary_flag_1) {
			cw++;

			if ((getTicks() > lastScreenChangeTicks + 2 * SCREEN_CHG_DELAY)
					&& mode_flag && (cw > 10)) {
				reinit_screen_flag = 1;
				oled_page_state = (oled_page_state + 1) % 7;

				cw = 0;
				lastScreenChangeTicks = getTicks();
			}

			rotary_flag_0 = 0;
			rotary_flag_1 = 0;
		}
		LPC_GPIOINT ->IO0IntClr = 1 << 25; //clear GPIO interrupt
	}
}

void check_joystick(void) {
	// Determine whether GPIO Interrupt P2.10 has occurred
	if ((LPC_GPIOINT ->IO0IntStatF >> 15) & 0x1) {
//		y++;
		if (oled_page_state == 6) {
			func_mode_selection = (
					func_mode_selection == 2 ? 0 : func_mode_selection + 1);

			func_change_flag = 1;
		}
		LPC_GPIOINT ->IO0IntClr = 1 << 15;
	}
	if ((LPC_GPIOINT ->IO0IntStatF >> 16) & 0x1) {
//		x++;

		//ensure delay between screen changes
		if ((getTicks() > lastScreenChangeTicks + SCREEN_CHG_DELAY)
				&& mode_flag) {
			reinit_screen_flag = 1;
			oled_page_state = (oled_page_state + 1) % 7;

			lastScreenChangeTicks = getTicks();
		}
		LPC_GPIOINT ->IO0IntClr = 1 << 16;
	}
	if ((LPC_GPIOINT ->IO0IntStatF >> 17) & 0x1) {
		// centre button
		LPC_GPIOINT ->IO0IntClr = 1 << 17;
	}
	if ((LPC_GPIOINT ->IO2IntStatF >> 3) & 0x1) {
		if (oled_page_state == 6) {
			func_mode_selection = (
					func_mode_selection == 0 ? 2 : func_mode_selection - 1);

			func_change_flag = 1;
		}
//		y--;
		LPC_GPIOINT ->IO2IntClr = 1 << 3;
	}
	if ((LPC_GPIOINT ->IO2IntStatF >> 4) & 0x1) {
//		x--;
		if ((getTicks() > lastScreenChangeTicks + SCREEN_CHG_DELAY)
				&& mode_flag) {
			reinit_screen_flag = 1;
			oled_page_state = (oled_page_state == 0 ? 6 : oled_page_state - 1);

			lastScreenChangeTicks = getTicks();
		}
		LPC_GPIOINT ->IO2IntClr = 1 << 4;
	}
}

// EINT3 Interrupt Handler
void EINT3_IRQHandler(void) {
	// Determine if GPIO Interrupt P2.5 has occurred (ISL2900023)
	if ((LPC_GPIOINT ->IO2IntStatF >> 5) & 0x1) {
		//clear interrupts
		LPC_GPIOINT ->IO2IntClr = 1 << 5; //clear GPIO interrupt
		light_clearIrqStatus(); //clear peripheral interrupt

		//darkness detected
		if (detect_darkness_flag == 1) {

			lightSensor_detectLight();
			detect_darkness_flag = 0;
		} else if (detect_darkness_flag == 0) {

			lightSensor_detectDarkness();
			detect_darkness_flag = 1;
		}
	}

	check_rotary_switch();
	check_joystick();
}

/*** OLED functions for monitor mode ***/

void graphics_glitch_fix() {
	oled_putPixel(95, 47, OLED_COLOR_BLACK);
}

//prints empty labels and 'monitor' on oled
void monitor_oled_init(void) {
	oled_putString(1, 1, STR_MAIN_TITLE, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_rect(0, 10, 95, 62, OLED_COLOR_WHITE);
	oled_putString(2, 12, STR_MAIN_LUX, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 22, STR_MAIN_TEMP, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 32, STR_MAIN_ACCX, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 42, STR_MAIN_ACCY, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 52, STR_MAIN_ACCZ, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	graphics_glitch_fix();
}

void monitor_oled_temp(void) {
	oled_putBigString(20, 1, STR_BIG_TEMP, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			2);
	graphics_glitch_fix();
}

void monitor_oled_light(void) {
	oled_putBigString(30, 1, STR_BIG_LIGHT, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			2);
	graphics_glitch_fix();
}

void monitor_oled_accX(void) {
	oled_putBigString(15, 1, STR_BIG_ACCX, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			2);
	graphics_glitch_fix();
}

void monitor_oled_accY(void) {
	oled_putBigString(15, 1, STR_BIG_ACCY, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			2);
	graphics_glitch_fix();
}

void monitor_oled_accZ(void) {
	oled_putBigString(15, 1, STR_BIG_ACCZ, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			2);
	graphics_glitch_fix();
}

void monitor_oled_func(void) {
	oled_putString(1, 1, STR_FUNC_TITLE, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	//selection boxes
	oled_rect(0, 10, 95, 23, OLED_COLOR_WHITE);
	oled_rect(0, 23, 95, 36, OLED_COLOR_WHITE);
	oled_rect(0, 36, 95, 49, OLED_COLOR_WHITE);

	//selection text
	oled_putString(9, 13, STR_FUNC_1, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(9, 27, STR_FUNC_2, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(9, 39, STR_FUNC_3, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	update_selectArrow_oled();

	graphics_glitch_fix();
}

//update sampled data on oled
void displaySampledData_oled(void) {
	//update OLED
	sprintf(tempStr, STR_UINTVALUES_OUTPUT, light_reading);
	oled_putString(35, 12, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(tempStr, STR_FLOATVALUES_OUTPUT, temperature_reading / 10.0);
	oled_putString(35, 22, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(tempStr, STR_INTVALUES_OUTPUT, accX - accInitX);
	oled_putString(35, 32, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(tempStr, STR_INTVALUES_OUTPUT, accY - accInitY);
	oled_putString(35, 42, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	sprintf(tempStr, STR_INTVALUES_OUTPUT, 64 + accZ - accInitZ);
	oled_putString(35, 52, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	graphics_glitch_fix();
}

//helper functions to display values for diff screens
void displayTempLarge_oled(void) {
	sprintf(tempStr, STR_FLOATVALUES_OUTPUT, temperature_reading / 10.0);
	oled_putBigString(15, 27, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			font_size);

	graphics_glitch_fix();
}

void displayLightLarge_oled(void) {
	sprintf(tempStr, STR_UINTVALUES_OUTPUT, light_reading);
	oled_putBigString(25, 27, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			font_size);

	graphics_glitch_fix();
}

void displayAccXLarge_oled(void) {
	sprintf(tempStr, STR_INTVALUES_OUTPUT, accX - accInitX);
	oled_putBigString(35, 27, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			font_size);

	graphics_glitch_fix();
}

void displayAccYLarge_oled(void) {
	sprintf(tempStr, STR_INTVALUES_OUTPUT, accY - accInitY);
	oled_putBigString(35, 27, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			font_size);

	graphics_glitch_fix();
}

void displayAccZLarge_oled(void) {
	sprintf(tempStr, STR_INTVALUES_OUTPUT, 64 + accZ - accInitZ);
	oled_putBigString(35, 27, tempStr, OLED_COLOR_WHITE, OLED_COLOR_BLACK,
			font_size);

	graphics_glitch_fix();
}

void update_selectArrow_oled(void) {
	//clear the previous arrow
	oled_putString(2, 13, STR_BLANK_CHAR, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 26, STR_BLANK_CHAR, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(2, 39, STR_BLANK_CHAR, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	oled_putString(2, 13 * (1 + func_mode_selection), STR_ARROW_CHAR,
			OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	graphics_glitch_fix();
}

void reinit_oled(void) {
	oled_clearScreen(OLED_COLOR_BLACK);
	switch (oled_page_state) {
	case 0:
		monitor_oled_init();
		break;
	case 1:
		monitor_oled_temp();
		break;
	case 2:
		monitor_oled_light();
		break;
	case 3:
		monitor_oled_accX();
		break;
	case 4:
		monitor_oled_accY();
		break;
	case 5:
		monitor_oled_accZ();
		break;
	case 6:
		monitor_oled_func();
		break;
	}
}

void update_oled() {
	//display data to relevant screen
	switch (oled_page_state) {
	case 0:
		displaySampledData_oled();
		break;
	case 1:
		displayTempLarge_oled();
		break;
	case 2:
		displayLightLarge_oled();
		break;
	case 3:
		displayAccXLarge_oled();
		break;
	case 4:
		displayAccYLarge_oled();
		break;
	case 5:
		displayAccZLarge_oled();
		break;
	}
}

void prep_monitorMode(void) {
	//un-reset clocks
	LPC_TIM1 ->TCR = (0 << 1);
	LPC_TIM2 ->TCR = (0 << 1);
	//re-enable
	init_timer1();
	init_timer2();

	read_acc(&accInitX, &accInitY, &accInitZ);

	monitor_oled_init();
	sseg_controller();

	// sample sensors + update OLED
	sample_sensors();
	update_oled(oled_page_state);

	UART_SendString(LPC_UART3, STR_MONITOR_MODE);
}

//reset devices and disable timers
void prep_passiveMode(void) {
	//off everything
	oled_clearScreen(OLED_COLOR_BLACK); //clear OLED
	led7seg_setChar(0x00, 0);			//off 7 segment
	timer2count = 0;						//reset 7 segment counter
	rgb_setLeds(0x00);	//off RGB led
	pca9532_setLeds(0x00, 0xFFFF); // off led_array
	GPIO_ClearValue(2, 1 << 8); //off ext LED
	GPIO_ClearValue(0, 1 << 26); //off siren

	//reset clocks
	LPC_TIM1 ->TCR = (1 << 1);
	LPC_TIM2 ->TCR = (1 << 1);
	//disable timers
	LPC_TIM1 ->TCR = (0 << 0);
	LPC_TIM2 ->TCR = (0 << 0);

	//reset flags
	temp_high_flag = 0;
	detect_darkness_flag = 1;
	movement_detected_flag = 0;
	speaker_on_flag = 0;
	func_change_flag = 0;

	//reset page
	oled_page_state = 0;
	func_mode_selection = 0;

	//reset RGB flag
	rgbLED_mask = 0x00;
}

void read_acc(int8_t* accX, int8_t* accY, int8_t* accZ) {
	//poll acc sensor
	acc_read(accX, accY, accZ);

	//check for movement and update accOld
	if ((*accX - accOldX > 5) || (*accY - accOldY > 5)
			|| (*accZ - accOldZ > 5)) {
		movement_detected_flag = 1;
	}

	accOldX = *accX;
	accOldY = *accY;
	accOldZ = *accZ;
}

//sample the accelerometer, light, temperature sensors
void sample_sensors(void) {
	//poll light sensor
	light_reading = light_read();
	//poll acc sensor
	read_acc(&accX, &accY, &accZ);
	//poll temp sensor
	temperature_reading = temp_read();
}

/*** function mode executor ***/
void execute_function(void) {
	switch (func_mode_selection) {
	case 0:
		speaker_on_flag = !speaker_on_flag;
		if (!speaker_on_flag) {
			GPIO_ClearValue(0, 1 << 26); //make sure speaker is off
		}
		break;
	case 1:
		notify_cems();
		break;
	case 2:
		leds_toggle_flag = !leds_toggle_flag;
		ledArray_controller();
		extLED_controller();
		break;
	case 3:
		break;
	}
}

//transmit message through UART
void transmitData() {
	if (((rgbLED_mask & RGB_RED) >> 0) == 1) {
		UART_SendString(LPC_UART3, STR_FIRE_ALERT);
	}

	if (((rgbLED_mask & RGB_BLUE) >> 1) == 1) {
		UART_SendString(LPC_UART3, STR_DARK_ALERT);
	}

	static uint8_t transmitCount = 0;

	char string[50];

	snprintf(string, 50, "%03d_-_T-%.2f_L-%d_AX.%d_AY.%d_AZ.%d\r\n",
			transmitCount++, temperature_reading / 10.0, light_reading,
			(int) (accX - accInitX), (int) (accY - accInitY),
			(int) (accZ - accInitZ));

	UART_SendString(LPC_UART3, &string);
}

//send SOS message to CEMS
void notify_cems() {
	char string[50];

	snprintf(string, 50, STR_CEMS_ALERT, userID);

	UART_SendString(LPC_UART3, &string);
}

void initial_setup(int8_t* accInitX, int8_t* accInitY, int8_t* accInitZ) {
	//SysTick init
	SysTick_Config(SystemCoreClock / 1000);

	init_protocols();
	init_peripherals();
	init_GPIO();
	init_interrupts();

	//hardware setup
	light_setIrqInCycles(LIGHT_CYCLE_1);
	light_enable(); //enable light sensor
	oled_clearScreen(OLED_COLOR_BLACK); //clear oled
	acc_init();
	read_acc(accInitX, accInitY, accInitZ);

	prep_passiveMode();
}

int main(void) {
	initial_setup(&accInitX, &accInitY, &accInitZ);
	//main execution loop
	while (1) {
		//stable, passive mode
		if (mode_flag == 0) {
			prep_passiveMode();
			while (mode_flag == 0)
				; //wait for MONITOR to be enabled
			prep_monitorMode();
		}

//		//slower, delay but much less likely to crash
//		if (rgbLED_flag
//				&& ((((rgbLED_mask & RGB_BLUE) >> 1))
//						|| ((rgbLED_mask & RGB_RED) >> 0))) {
//			rgbLED_controller();
//			rgbLED_flag = 0;
//		}
		if (sseg_flag) {
			sseg_controller();
			sseg_flag = 0;
		}

		//init the screens
		if (reinit_screen_flag) {
			reinit_oled();
			reinit_screen_flag = 0;
		}

		//sample sensors every 5s, else sample temp and acc every 0.1s
		if (sample_sensors_flag) {
			sample_sensors();
			sample_sensors_flag = 0;

			//display data to relevant screen
			update_oled(oled_page_state);
		} else if (getTicks() > oldSampleTicks + 100) {
			temperature_reading = temp_read();
			read_acc(&accX, &accY, &accZ);

			oldSampleTicks = getTicks();
		}

		//if high temperature is detected
		if (temperature_reading >= (TEMP_HIGH_WARNING - DEBUG_HEAT_OFFSET)) {
			rgbLED_mask |= RGB_RED;
		}

		//if MOVEMENT_DETECTED
		if (movement_detected_flag) {
			//if no movement in darkness after set duration, disable movement flag
			if (getTicks() > lastMotionDetectedTicks + 20) {
				//check for prolonged movement detection, if so, set flag
				if (!detect_darkness_flag) {
					rgbLED_mask |= RGB_BLUE; //toggle blue led mask on
				} else {
					movement_detected_flag = 0;
				}
			}
		}

		//if at func mode screen
		if (func_change_flag && oled_page_state == 6) {
			update_selectArrow_oled();
			func_change_flag = 0;
		}

		//execute function if selected
		if (func_execute_flag) {
			execute_function();
			func_execute_flag = 0;
		}

		//drive piezo speaker for siren
		if (speaker_on_flag) {
			speaker_controller();
		}

		//if need to transmit
		if (send_message_flag) {
			transmitData();
			send_message_flag = 0;
		}
	}
	return 0;
}

