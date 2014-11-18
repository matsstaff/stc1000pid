/*
 * STC1000+, improved firmware and Arduino based firmware uploader for the STC-1000 dual stage thermostat.
 *
 * Copyright 2014 Mats Staffansson
 *
 * This file is part of STC1000+.
 *
 * STC1000+ is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * STC1000+ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with STC1000+.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define __16f1828
#include "pic14/pic16f1828.h"
#include "stc1000pi.h"

/* Defines */
#define ClrWdt() { __asm CLRWDT __endasm; }

/* Configuration words */
unsigned int __at _CONFIG1 __CONFIG1 = 0xFD4;
unsigned int __at _CONFIG2 __CONFIG2 = 0x3AFF;

/* Temperature lookup table  */
#ifdef FAHRENHEIT
const int ad_lookup[] = { 0, -555, -319, -167, -49, 48, 134, 211, 282, 348, 412, 474, 534, 593, 652, 711, 770, 831, 893, 957, 1025, 1096, 1172, 1253, 1343, 1444, 1559, 1694, 1860, 2078, 2397, 2987 };
#else  // CELSIUS
const int ad_lookup[] = { 0, -486, -355, -270, -205, -151, -104, -61, -21, 16, 51, 85, 119, 152, 184, 217, 250, 284, 318, 354, 391, 431, 473, 519, 569, 624, 688, 763, 856, 977, 1154, 1482 };
#endif

/* LED character lookup table (0-15), includes hex */
//unsigned const char led_lookup[] = { 0x3, 0xb7, 0xd, 0x25, 0xb1, 0x61, 0x41, 0x37, 0x1, 0x21, 0x5, 0xc1, 0xcd, 0x85, 0x9, 0x59 };
/* LED character lookup table (0-9) */
unsigned const char led_lookup[] = { LED_0, LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8, LED_9 };

/* Global variables to hold LED data (for multiplexing purposes) */
led_e_t led_e = {0xff};
led_t led_10, led_1, led_01;

static int temperature=0;
static int temperature2=0;
static unsigned char output = 0;

/* Functions.
 * Note: Functions used from other page cannot be static, but functions
 * not used from other page SHOULD be static to decrease overhead.
 * Functions SHOULD be defined before used (ie. not just declared), to
 * decrease overhead. Refer to SDCC manual for more info.
 */

/* Read one configuration data from specified address.
 * arguments: Config address (0-127)
 * return: the read data
 */
unsigned int eeprom_read_config(unsigned char eeprom_address){
	unsigned int data = 0;
	eeprom_address = (eeprom_address << 1);

	do {
		EEADRL = eeprom_address; // Data Memory Address to read
		CFGS = 0; // Deselect config space
		EEPGD = 0; // Point to DATA memory
		RD = 1; // Enable read

		data = ((((unsigned int) EEDATL) << 8) | (data >> 8));
	} while(!(eeprom_address++ & 0x1));

	return data; // Return data
}

/* Store one configuration data to the specified address.
 * arguments: Config address (0-127), data
 * return: nothing
 */
void eeprom_write_config(unsigned char eeprom_address,unsigned int data)
{
	// Avoid unnecessary EEPROM writes
	if(data == eeprom_read_config(eeprom_address)){
		return;
	}

	// multiply address by 2 to get eeprom address, as we will be storing 2 bytes.
	eeprom_address = (eeprom_address << 1);

	do {
		// Address to write
	    EEADRL = eeprom_address;
	    // Data to write
	    EEDATL = (unsigned char) data;
	    // Deselect configuration space
	    CFGS = 0;
	    //Point to DATA memory
	    EEPGD = 0;
	    // Enable write
	    WREN = 1;

	    // Disable interrupts during write
	    GIE = 0;

	    // Write magic words to EECON2
	    EECON2 = 0x55;
	    EECON2 = 0xAA;

	    // Initiate a write cycle
	    WR = 1;

	    // Re-enable interrupts
	    GIE = 1;

	    // Disable writes
	    WREN = 0;

	    // Wait for write to complete
	    while(WR);

	    // Clear write complete flag (not really needed
	    // as we use WR for check, but is nice)
	    EEIF=0;

	    // Shift data for next pass
	    data = data >> 8;

	} while(!(eeprom_address++ & 0x01)); // Run twice for 16 bits

}

static int iabs(int ia){
	if(ia < 0){
		ia = -ia;
	}
	return ia;
}


/* Interrupt service routine.
 * Receives timer2 interrupts every millisecond.
 * Handles multiplexing of the LEDs.
 */
static unsigned char counter=0, cv=0;
static void interrupt_service_routine(void) __interrupt 0 {

	if (TMR0IF) {
		unsigned char latb = (LATB << 1);

		if(latb == 0){
			latb = 0x10;
		}

		TRISC = 0; // Ensure LED data pins are outputs
		LATB = 0; // Disable LED's while switching

		// Multiplex LED's every millisecond
		switch(latb) {
			case 0x10:
			LATC = led_10.raw;
			break;
			case 0x20:
			LATC = led_1.raw;
			break;
			case 0x40:
			LATC = led_01.raw;
			break;
			case 0x80:
			LATC = led_e.raw;
			break;
		}

		// Enable new LED
		LATB = latb;

		// Clear interrupt flag
		TMR0IF = 0;
	}

	if(TMR2IF) {

		if(counter == 0){
			cv = output;
			counter++;
		}

		LATA5 = (counter <= cv);
		led_e.e_heat = !LATA5;

		LATA4 = TMR4ON;
//		led_e.e_cool = !LATA4;

		counter++;

		// Reset timer flag
		TMR2IF = 0;
	}
}

#define START_TCONV_1()		(ADCON0 = _CHS1 | _ADON)
#define START_TCONV_2()		(ADCON0 = _CHS0 | _ADON)

static unsigned int read_ad(unsigned int adfilter){
	ADGO = 1;
	while(ADGO);
	ADON = 0;
	return ((adfilter - (adfilter >> AD_FILTER_SHIFT)) + ((ADRESH << 8) | ADRESL));
}

static int ad_to_temp(unsigned int adfilter){
	unsigned char i;
	long temp = 32;
	unsigned char a = ((adfilter >> (AD_FILTER_SHIFT - 1)) & 0x3f); // Lower 6 bits
	unsigned char b = ((adfilter >> (AD_FILTER_SHIFT + 5)) & 0x1f); // Upper 5 bits


	// Interpolate between lookup table points
	for (i = 0; i < 64; i++) {
		if(a <= i) {
			temp += ad_lookup[b];
		} else {
			temp += ad_lookup[b + 1];
		}
	}

	// Divide by 64 to get back to normal temperature
	return (temp >> 6);
}


unsigned char output_start=72;
unsigned char output_swing=64;
unsigned char hyst=2;

enum at_fsm_state {
	AT_IDLE = 0,
	AT_GO,
	AT_SETTLE,
	AT_INIT,
	AT_FIND_MIN,
	AT_FIND_MAX,
	AT_DONE,
	AT_FAIL
};

unsigned char at_state = AT_IDLE;
static unsigned int ms960_cnt=0;
unsigned int peaks[4];
int peaks_t[2];
int base_temperature;
unsigned char peak_count;

static int diff(){
	int min=peaks[0];
	int max=peaks[0];
	unsigned char i;
	
	for(i=1; i<4; i++){
		if(peaks[i] < min){
			min = peaks[i];
		}
		if(peaks[i] > max){
			max = peaks[i];
		}
	}
	return max-min;
}


static void autotune_fsm(){

	ms960_cnt++;

	switch(at_state){
		case AT_IDLE:
			output = 0;
		break;
		case AT_GO:
			output = output_start;
			ms960_cnt = 0;
			at_state = AT_SETTLE;
		break;
		case AT_SETTLE:
			led_e.e_point = ms960_cnt & 0x1;
			output = output_start;
			if((ms960_cnt & 0x1F) == 0){
				peaks[(ms960_cnt >> 5) & 0x3] = temperature;
				if((ms960_cnt >> 5) >= 3 && diff() <= hyst){
					output = output_start - output_swing;
					base_temperature = (peaks[0]+peaks[1]+peaks[2]+peaks[3]) >> 2;
					led_e.e_point = 1;
					at_state = AT_INIT;
				} else if((ms960_cnt >> 5) >= 30){
					at_state = AT_FAIL;
				}
			}
		break;
		case AT_INIT:
			if(temperature < base_temperature - hyst){
				output = output_start + output_swing;
				peak_count = 0;
				peaks[peak_count & 0x3] = temperature;
				led_e.e_point = 0;
				ms960_cnt = 0;
				at_state = AT_FIND_MIN;
			}
		break;
		case AT_FIND_MIN:
			if(temperature < peaks[peak_count & 0x3]){
				peaks[peak_count & 0x3] = temperature;
			}
			if(temperature > base_temperature + hyst){
				peak_count++;
				peaks[peak_count & 0x3] = temperature;
				peaks_t[((peak_count >> 1) & 0x1)] = ms960_cnt;
				output = output_start - output_swing;
				led_e.e_point = 1;
				at_state = AT_FIND_MAX;
			}
		break;
		case AT_FIND_MAX:
			if(temperature > peaks[peak_count]){
				peaks[peak_count & 0x3] = temperature;
				peaks_t[((peak_count >> 1) & 0x1)] = ms960_cnt;
			}
			if(temperature < base_temperature - hyst){
				if(peak_count >= 3) {
//					int a1 = peaks[1]-peaks[0];
//					int a2 = peaks[3]-peaks[2];
//					a1=abs(a1);
//					a2=abs(a2);
//					a1-=a2;
//					a1=abs(a1);
//					if(((a2+8)>>4) > a1){

					int a1 = peaks[2]-peaks[0];
					int a2 = peaks[3]-peaks[1];
					a1 = iabs(a1);
					a2 = iabs(a2);
					if(a1 <= hyst && a2 <= hyst){
						int cp;
						int ci;

						a2 = peaks[peak_count & 0x3]-peaks[(peak_count -1) & 0x3];
						a2 = iabs(a2);
						a1 = peaks_t[1] - peaks_t[0];
						a1 = iabs(a1);

//						cp = (2 * 260 * output_swing) / a2;
//						ci = (2 * 626 * output_swing) / (a2 * dur);

/*
						cp = ( (3441 * ((unsigned long)output_swing)) / ((unsigned long)a2) ) >> REG_P_PRESCALE;
						ci = ( (6883 * ((unsigned long)output_swing) ) /  ( ((unsigned long)a2) * ((unsigned long)a1) ) ) >> REG_I_PRESCALE;
						base_temperature = ci;
						ci = ( (1146 * ((unsigned long)output_swing) * ((unsigned long)a1)) / ((unsigned long)a2)  ) >> REG_D_PRESCALE;
*/
						cp = ( (ZN_P * ((unsigned long)output_swing)) / ((unsigned long)a2) ) >> REG_P_PRESCALE;
						ci = ( (ZN_I * ((unsigned long)output_swing) ) /  ( ((unsigned long)a2) * ((unsigned long)a1) ) ) >> REG_I_PRESCALE;
						base_temperature = ci;
						ci = ( (ZN_D * ((unsigned long)output_swing) * ((unsigned long)a1)) / ((unsigned long)a2)  ) >> REG_D_PRESCALE;


						eeprom_write_config(EEADR_SET_MENU_ITEM(cP), cp);
						eeprom_write_config(EEADR_SET_MENU_ITEM(cI), base_temperature);
						eeprom_write_config(EEADR_SET_MENU_ITEM(cd), ci);

						at_state = AT_DONE;
						break;
					} else if(peak_count >= 19){
						at_state = AT_FAIL;
						break;
					}
				}
				peak_count++;
				peaks[peak_count & 0x3] = temperature;
				output = output_start + output_swing;
				led_e.e_point = 0;
				at_state = AT_FIND_MIN;
			}
		break;
		case AT_DONE:
			led_e.e_point = 1;
			output = 0;
		break;
		case AT_FAIL:
			led_e.e_point = 1;
			output = 0;
			at_state = AT_IDLE;
		break;

	}

}

static void update_period(unsigned char period){
	T2CON = ((period & 0x1) << 6) | _T2OUTPS2 | _T2OUTPS1 | _T2OUTPS0 | _TMR2ON | (((period & 0x6)>> 1) + 1);
}

/* Initialize hardware etc, on startup.
 * arguments: none
 * returns: nothing
 */
void init() {

//   OSCCON = 0b01100010; // 2MHz
	OSCCON = 0b01101010; // 4MHz

	// Heat, cool as output, Thermistor as input, piezo output
	TRISA = 0b00001110;
	LATA = 0; // Drive relays and piezo low

	// LED Common anodes
	TRISB = 0;
	LATB = 0;

	// LED data (and buttons) output
	TRISC = 0;

	// Analog input on thermistor
	ANSELA = _ANSA1 | _ANSA2;
	// Select AD channel AN2
//	ADCON0bits.CHS = 2;
	// AD clock FOSC/8 (FOSC = 4MHz)
	ADCS0 = 1;
	// Right justify AD result
	ADFM = 1;
	// Enable AD
//	ADON = 1;
	// Start conversion
//	ADGO = 1;

	// IMPORTANT FOR BUTTONS TO WORK!!! Disable analog input -> enables digital input
	ANSELC = 0;


	//Timer0 Registers Prescaler= 8 - TMR0 Preset = 0 - Freq = 488.28 Hz - Period = 0.002048 seconds
//	T0CS = 0;  // bit 5  TMR0 Clock Source Select bit...0 = Internal Clock (CLKO) 1 = Transition on T0CKI pin
//	T0SE = 0;  // bit 4 TMR0 Source Edge Select bit 0 = low/high 1 = high/low
//	PSA = 0;   // bit 3  Prescaler Assignment bit...0 = Prescaler is assigned to the Timer0
//	PS2 = 0;   // bits 2-0  PS2:PS0: Prescaler Rate Select bits
//	PS1 = 1;
//	PS0 = 0;
	OPTION_REG = _NOT_WPUEN | _INTEDG | _PS1 | _PS0;
	TMR0 = 0;             // preset for timer register
//	TMR0IE = 1;

	update_period((unsigned char)eeprom_read_config(EEADR_SET_MENU_ITEM(Pd)));
	PR2 = 244;
	// Enable Timer2 interrupt
	TMR2IE = 1;

	// Postscaler 1:1, Enable counter, prescaler 1:4
//	T2CON = 0b00000101;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:4-> 250kHz, 250 gives interrupt every 1 ms
//	PR2 = 250;
	// Enable Timer2 interrupt
//	TMR2IE = 1;

	// Postscaler 1:15, - , prescaler 1:16
	T4CON = 0b01110010;
	TMR4ON = eeprom_read_config(EEADR_POWER_ON);
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:16-> 62.5kHz, 250 and postscale 1:15 -> 16.66666 Hz or 60ms
	PR4 = 250;

	// Postscaler 1:15, Enable counter, prescaler 1:16
	T6CON = 0b01110110;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:16-> 62.5kHz, 250 and postscale 1:15 -> 16.66666 Hz or 60ms
	PR6 = 250;

	// Set PEIE (enable peripheral interrupts, that is for timer2) and GIE (enable global interrupts)
//	INTCON = 0b11000000;
	INTCON = _GIE | _PEIE | _TMR0IE;

}



/*
 * Main entry point.
 */
void main(void) __naked {
	unsigned int millisx60=0;
	unsigned int ad_filter =(0x7fff >> (6 - AD_FILTER_SHIFT));
	unsigned int ad_filter2=(0x7fff >> (6 - AD_FILTER_SHIFT));

	init();

	START_TCONV_1();

	//Loop forever
	while (1) {

		if(TMR6IF) {

			// Handle button press and menu
			button_menu_fsm();

			// Reset timer flag
			TMR6IF = 0;
		}

		if(TMR4IF) {

			millisx60++;

			if(millisx60 & 0x1){
				ad_filter = read_ad(ad_filter);
				START_TCONV_2();
			} else {
				ad_filter2 = read_ad(ad_filter2);
				START_TCONV_1();
			}

			// Only run every 16th time called, that is 16x60ms = 960ms
			// Close enough to 1s for our purposes.
			if((millisx60 & 0xf) == 0) {

				temperature = ad_to_temp(ad_filter) + eeprom_read_config(EEADR_SET_MENU_ITEM(tc));
				temperature2 = ad_to_temp(ad_filter2) + eeprom_read_config(EEADR_SET_MENU_ITEM(tc2));

				autotune_fsm();

				if(TMR1GE){
					temperature_to_led(temperature);
				}


			} // End 1 sec section

			// Reset timer flag
			TMR4IF = 0;
		}

		// Reset watchdog
		ClrWdt();
	}
}
