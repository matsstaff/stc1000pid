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

#if 1
static unsigned int divu10(unsigned int n) {
	unsigned int q, r;
	q = (n >> 1) + (n >> 2);
	q = q + (q >> 4);
	q = q + (q >> 8);
	q = q >> 3;
	r = n - ((q << 3) + (q << 1));
	return q + ((r + 6) >> 4);
}
#else
#define divu10(x)	((x)/10)
#endif

/* Update LED globals with temperature or integer data.
 * arguments: value (actual temperature multiplied by 10 or an integer)
 *            decimal indicates if the value is multiplied by 10 (i.e. a temperature)
 * return: nothing
 */
void value_to_led(int value, unsigned char decimal) {
	unsigned char i;

	// Handle negative values
	if (value < 0) {
		led_e.e_negative = 0;
		value = -value;
	} else {
		led_e.e_negative = 1;
	}

	// This assumes that only temperatures and all temperatures are decimal
	if(decimal){
		led_e.e_deg = 0;
#ifdef FAHRENHEIT
		led_e.e_c = 1;
#else
		led_e.e_c = 0;
#endif // FAHRENHEIT
	}

	// If temperature >= 100 we must lose decimal...
	if (value >= 1000) {
		value = divu10((unsigned int) value);
		decimal = 0;
	}

	// Convert value to BCD and set LED outputs
	if(value >= 100){
		for(i=0; value >= 100; i++){
			value -= 100;
		}
		led_10.raw = led_lookup[i & 0xf];
	} else {
		led_10.raw = LED_OFF; // Turn off led if zero (lose leading zeros)
	}
	if(value >= 10 || decimal || led_10.raw!=LED_OFF){ // If decimal, we want 1 leading zero
		for(i=0; value >= 10; i++){
			value -= 10;
		}
		led_1.raw = led_lookup[i];
		if(decimal){
			led_1.decimal = 0;
		}
	} else {
		led_1.raw = LED_OFF; // Turn off led if zero (lose leading zeros)
	}
	led_01.raw = led_lookup[(unsigned char)value];
}

void update_period(unsigned char period){
	T2CON = ((period & 0x1) << 6) | _T2OUTPS2 | _T2OUTPS1 | _T2OUTPS0 | _TMR2ON | (((period & 0x6)>> 1) + 1);
}

/* To be called once every minute.
 * Updates EEPROM configuration when running profile.
 */
static void update_profile(){
	unsigned char profile_no = eeprom_read_config(EEADR_SET_MENU_ITEM(rn));

	// Running profile?
	if (profile_no < CONSTANT_TEMPERATURE_MODE) {
		unsigned char curr_step = eeprom_read_config(EEADR_SET_MENU_ITEM(St));
		unsigned int curr_dur = eeprom_read_config(EEADR_SET_MENU_ITEM(dh)) + 1;
		unsigned char profile_step_eeaddr;
		unsigned int profile_step_dur;
		int profile_next_step_sp;

		// Sanity check
		if(curr_step > 8){
			curr_step = 8;
		}

		profile_step_eeaddr = EEADR_PROFILE_SETPOINT(profile_no, curr_step);
		profile_step_dur = eeprom_read_config(profile_step_eeaddr + 1);
		profile_next_step_sp = eeprom_read_config(profile_step_eeaddr + 2);

		// Reached end of step?
		if (curr_dur >= profile_step_dur) {
			// Update setpoint with value from next step
			eeprom_write_config(EEADR_SET_MENU_ITEM(SP), profile_next_step_sp);
			// Is this the last step (next step is number 9 or next step duration is 0)?
			if (curr_step == 8 || eeprom_read_config(profile_step_eeaddr + 3) == 0) {
				// Switch to constant temp mode.
				eeprom_write_config(EEADR_SET_MENU_ITEM(rn), CONSTANT_TEMPERATURE_MODE);
				return; // Fastest way out...
			}
			// Reset duration
			curr_dur = 0;
			// Update step
			curr_step++;
			eeprom_write_config(EEADR_SET_MENU_ITEM(St), curr_step);
		}
		// Update duration
		eeprom_write_config(EEADR_SET_MENU_ITEM(dh), curr_dur);
	}
}

/* Due to a fault in SDCC, static local variables are not initialized
 * properly, so the variables below were moved from temperature_control()
 * and made global.
 */
long integral=0;
unsigned volatile char output=0;
#ifdef FAHRENHEIT
#define PI_SCALE	12
#else
#define PI_SCALE	11
#endif
static void pi_control(int temperature){
	long tmp_out;

	if(eeprom_read_config(EEADR_SET_MENU_ITEM(rn)) <= 6){
		long tmp_v;
		int error = eeprom_read_config(EEADR_SET_MENU_ITEM(SP)) - temperature;

		integral += ((long)eeprom_read_config(EEADR_SET_MENU_ITEM(cI)) * error);	// Update integral
		tmp_out = ((long)eeprom_read_config(EEADR_SET_MENU_ITEM(cP)) * error) << 2;	// P

		tmp_out += integral; // I

		// Clamp output and integral
		tmp_v = ((long)eeprom_read_config(EEADR_SET_MENU_ITEM(OH))) << PI_SCALE;
		if(tmp_out > tmp_v){
			integral -= (tmp_out - tmp_v);
			tmp_out = tmp_v;
		}
		tmp_v = ((long)eeprom_read_config(EEADR_SET_MENU_ITEM(OL))) << PI_SCALE;
		if(tmp_out < tmp_v){
			integral += (tmp_v - tmp_out);
			tmp_out = tmp_v;
		}

		tmp_out >>= PI_SCALE;

	} else {
		tmp_out = eeprom_read_config(EEADR_SET_MENU_ITEM(OP));
	}

    output = (unsigned char)tmp_out;
}

/* Initialize hardware etc, on startup.
 * arguments: none
 * returns: nothing
 */
static void init() {

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

/* Interrupt service routine.
 * Receives timer2 interrupts every millisecond.
 * Handles multiplexing of the LEDs.
 */
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
		// TODO move to globals (static init broken)
		unsigned static char counter=0, cv=0;

		++counter;
		if(!counter){
			cv = output;
		}

		LATA5 = (counter <= cv) && cv && TMR4ON;
		led_e.e_heat = !LATA5;

		LATA4 = TMR4ON;
		led_e.e_cool = !LATA4;

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
	return ((adfilter - (adfilter >> 6)) + ((ADRESH << 8) | ADRESL));
}

static int ad_to_temp(unsigned int adfilter){
	unsigned char i;
	long temp = 32;
	unsigned char a = ((adfilter >> 5) & 0x3f); // Lower 6 bits
	unsigned char b = ((adfilter >> 11) & 0x1f); // Upper 5 bits

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

/*
 * Main entry point.
 */
void main(void) __naked {
	unsigned int millisx60=0;
	unsigned int ad_filter=0x7fff, ad_filter2=0x7fff;

	init();

	START_TCONV_1();

	//Loop forever
	while (1) {

		if(TMR6IF) {

			// Handle button press and menu
			button_menu_fsm();

			if(!TMR4ON){
				led_e.raw = LED_OFF;
				led_10.raw = LED_O;
				led_1.raw = led_01.raw = LED_F;
			}

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

				// Alarm on sensor error (AD result out of range)
				LATA0 = ((ad_filter>>8) >= 248 || (ad_filter>>8) <= 8) || (eeprom_read_config(EEADR_SET_MENU_ITEM(Pb)) && ((ad_filter2>>8) >= 248 || (ad_filter2>>8) <= 8));

				if(LATA0){ // On alarm, disable outputs
					led_10.raw = LED_A;
					led_1.raw = LED_L;
					led_e.raw = led_01.raw = LED_OFF;
					LATA4 = 0;
					LATA5 = 0;
				} else {
					// Update running profile every hour (if there is one)
					// and handle reset of millis x60 counter
					if(((unsigned char)eeprom_read_config(EEADR_SET_MENU_ITEM(rn))) < CONSTANT_TEMPERATURE_MODE){
						// Indicate profile mode
						led_e.e_set = 0;
						// Update profile every ~minute
						if(millisx60 >= 1000){
							update_profile();
							millisx60 = 0;
						}
					} else {
						led_e.e_set = 1;
						millisx60 = 0;
					}

					// Run PI Control
					pi_control(temperature);

					// Show temperature if menu is idle
					if(TMR1GE){
						led_e.e_point = !TX9;
						if(TX9){
							temperature_to_led(temperature2);
						} else {
							temperature_to_led(temperature);
						}
					}
				}

			} // End 1 sec section

			// Reset timer flag
			TMR4IF = 0;
		}

		// Reset watchdog
		ClrWdt();
	}
}
