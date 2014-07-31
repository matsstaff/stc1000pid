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
 *
 * Schematic of the connections to the MCU.
 *
 *                                     PIC16F1828
 *                                    ------------
 *                                VDD | 1     20 | VSS
 *                     Relay Heat RA5 | 2     19 | RA0/ICSPDAT (Programming header), Piezo buzzer
 *                     Relay Cool RA4 | 3     18 | RA1/ICSPCLK (Programming header)
 * (Programming header) nMCLR/VPP/RA3 | 4     17 | RA2/AN2 Thermistor
 *                          LED 5 RC5 | 5     16 | RC0 LED 0
 *                   LED 4, BTN 4 RC4 | 6     15 | RC1 LED 1
 *                   LED 3, BTN 3 RC3 | 7     14 | RC2 LED 2
 *                   LED 6, BTN 2 RC6 | 8     13 | RB4 LED Common Anode 10's digit
 *                   LED 7, BTN 1 RC7 | 9     12 | RB5 LED Common Anode 1's digit
 *        LED Common Anode extras RB7 | 10    11 | RB6 LED Common Anode 0.1's digit
 *                                    ------------
 *
 *
 * Schematic of the bit numbers for the display LED's. Useful if custom characters are needed.
 *
 *           * 7       --------    *    --------       * C
 *                    /   7   /    1   /   7   /       5 2
 *                 2 /       / 6    2 /       / 6    ----
 *                   -------          -------     2 / 7 / 6
 *           *     /   1   /        /   1   /       ---
 *           3  5 /       / 3    5 /       / 3  5 / 1 / 3
 *                --------    *    --------   *   ----  *
 *                  4         0      4        0    4    0
 *
 *
 *
 *
 */

#define __16f1828
#include "pic14/pic16f1828.h"
#include "stc1000pi.h"

/* Defines */
#define ClrWdt() { __asm CLRWDT __endasm; }

/* Configuration words */
unsigned int __at _CONFIG1 __CONFIG1 = 0xFDC;
unsigned int __at _CONFIG2 __CONFIG2 = 0x3AFF;

/* Temperature lookup table  */
#ifdef FAHRENHEIT
const int ad_lookup[32] = { 0, -555, -319, -167, -49, 48, 134, 211, 282, 348, 412, 474, 534, 593, 652, 711, 770, 831, 893, 957, 1025, 1096, 1172, 1253, 1343, 1444, 1559, 1694, 1860, 2078, 2397, 2987 };
#else  // CELSIUS
const int ad_lookup[32] = { 0, -486, -355, -270, -205, -151, -104, -61, -21, 16, 51, 85, 119, 152, 184, 217, 250, 284, 318, 354, 391, 431, 473, 519, 569, 624, 688, 763, 856, 977, 1154, 1482 };
#endif

/* LED character lookup table (0-15), includes hex */
unsigned const char led_lookup[16] = { 0x3, 0xb7, 0xd, 0x25, 0xb1, 0x61, 0x41, 0x37, 0x1, 0x21, 0x5, 0xc1, 0xcd, 0x85, 0x9, 0x59 };

/* Global variables to hold LED data (for multiplexing purposes) */
_led_e_bits led_e = {0xff};
unsigned char led_10, led_1, led_01;

static int temperature=0;

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

	// If temperature > 100 we must lose decimal...
	if (value >= 1000) {
		value = ((unsigned int) value) / 10;
		decimal = 0;
	}

	// Convert value to BCD and set LED outputs
	if(value >= 100){
		for(i=0; value >= 100; i++){
			value -= 100;
		}
		led_10 = led_lookup[i & 0xf];
	} else {
		led_10 = 0xff; // Turn off led if zero (lose leading zeros)
	}
	if(value >= 10 || decimal || led_10!=0xff){ // If decimal, we want 1 leading zero
		for(i=0; value >= 10; i++){
			value -= 10;
		}
		led_1 = led_lookup[i] & (decimal ? 0xfe : 0xff);
	} else {
		led_1 = 0xff; // Turn off led if zero (lose leading zeros)
	}
	led_01 = led_lookup[value];
}

void update_period(unsigned char period){
	T2CON = ((period & 0x1) << 6) | _T2OUTPS2 | _T2OUTPS1 | _T2OUTPS0 | _TMR2ON | (((period & 0x6)>> 1) + 1);
}

static void update_profile(){
	unsigned char profile_no;

	profile_no = eeprom_read_config(EEADR_RUN_MODE);

	if (profile_no < 6) { // Running profile
		unsigned char curr_step;
		unsigned int curr_dur;

		// Load step and duration
		curr_step = eeprom_read_config(EEADR_CURRENT_STEP);
		curr_step = curr_step > 8 ? 8 : curr_step;	// sanity check
		curr_dur = eeprom_read_config(EEADR_CURRENT_STEP_DURATION);

		curr_dur++;
		if (curr_dur >= eeprom_read_config(EEADR_PROFILE_DURATION(profile_no, curr_step))) {
			curr_step++;
			curr_dur = 0;
				// Is this the last step?
			if (curr_step == 9	|| eeprom_read_config(EEADR_PROFILE_DURATION(profile_no, curr_step)) == 0) {
				eeprom_write_config(EEADR_SETPOINT,	eeprom_read_config(EEADR_PROFILE_SETPOINT(profile_no, curr_step)));
				eeprom_write_config(EEADR_RUN_MODE, 6);
				return; // Fastest way out...
			}
			eeprom_write_config(EEADR_CURRENT_STEP, curr_step);
			eeprom_write_config(EEADR_SETPOINT, eeprom_read_config(EEADR_PROFILE_SETPOINT(profile_no, curr_step)));
		}
		eeprom_write_config(EEADR_CURRENT_STEP_DURATION, curr_dur);
	}
}

/* Due to a fault in SDCC, static local variables are not initialized
 * properly, so the variables below were moved from temperature_control()
 * and made global.
 */
//int last_temperature=0;
unsigned int integral=0;
unsigned char output=0;

static void pi_control(int temperature){
	int tmp_out;
	int tmp_v;

	if(eeprom_read_config(EEADR_RUN_MODE) <= 6){

		tmp_out = ((int)eeprom_read_config(EEADR_SETPOINT) - temperature);	// calc error

		// Clamp error
		if(tmp_out > 127){
			tmp_out = 127;
		}else if(tmp_out < -127){
			tmp_out = -127;
		}

		integral += (eeprom_read_config(EEADR_KI) * tmp_out);	// Update integral
		tmp_out *= eeprom_read_config(EEADR_KP);	// P

		tmp_out += integral; // I
//		tmp_out += eeprom_read_config(EEADR_KD) * (last_temperature - temperature); // D

		// Clamp output and integral
		tmp_v = eeprom_read_config(EEADR_MAX_OUTPUT) << 6;
		if(tmp_out > tmp_v){
			integral -= (tmp_out - tmp_v);
			tmp_out = tmp_v;
		}
		tmp_v = eeprom_read_config(EEADR_MIN_OUTPUT) << 6;
		if(tmp_out < tmp_v){
			integral += (tmp_v - tmp_out);
			tmp_out = tmp_v;
		}

		tmp_out >>= 6;

		if((unsigned char)tmp_out < (unsigned char)eeprom_read_config(EEADR_MIN_OUTPUT)){
			tmp_out = (unsigned char)eeprom_read_config(EEADR_MIN_OUTPUT);
		}

		if((unsigned char)tmp_out > (unsigned char)eeprom_read_config(EEADR_MAX_OUTPUT)){
			tmp_out = (unsigned char)eeprom_read_config(EEADR_MAX_OUTPUT);
		}

		// Remember last input
//		last_temperature = temperature;
	} else {
		tmp_out = eeprom_read_config(EEADR_OUTPUT);
	}

    // Update output (16-bit, need to disable interrupts)
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
	ANSA2 = 1;
	// Select AD channel AN2
	CHS1 = 1;
	// AD clock FOSC/8 (FOSC = 4MHz)
	ADCS0 = 1;
	// Right justify AD result
	ADFM = 1;
	// Enable AD
	ADON = 1;
	// Start conversion
	ADGO = 1;

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

	update_period((unsigned char)eeprom_read_config(EEADR_PERIOD));
	PR2 = 244;
	// Enable Timer2 interrupt
	TMR2IE = 1;

	// Postscaler 1:1, Enable counter, prescaler 1:4
//	T2CON = 0b00000101;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:4-> 250kHz, 250 gives interrupt every 1 ms
//	PR2 = 250;
	// Enable Timer2 interrupt
//	TMR2IE = 1;

	// Postscaler 1:15, Enable counter, prescaler 1:16
	T4CON = 0b01110110;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:16-> 62.5kHz, 250 and postscale 1:15 -> 16.66666 Hz or 60ms
	PR4 = 250;

	// Postscaler 1:1, Enable counter, prescaler 1:16
//	T4CON = 0b00000111;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:16-> ???kHz, 122 -> 512.3Hz
//	PR4 = 122;

	// Postscaler 1:1, Enable counter, prescaler 1:4
//	T4CON = 0b00000101;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:4-> 250kHz, 250 gives interrupt every 1 ms
//	PR4 = 250;

	// Postscaler 1:15, Enable counter, prescaler 1:16
	T6CON = 0b01110110;
	// @4MHz, Timer 2 clock is FOSC/4 -> 1MHz prescale 1:16-> 62.5kHz, 250 and postscale 1:15 -> 16.66666 Hz or 60ms
	PR6 = 250;

	// Set PEIE (enable peripheral interrupts, that is for timer2) and GIE (enable global interrupts)
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
			LATC = led_10;
			break;
			case 0x20:
			LATC = led_1;
			break;
			case 0x40:
			LATC = led_01;
			break;
			case 0x80:
			LATC = led_e.led_e;
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

		LATA5 = (counter <= cv) && cv;
		led_e.e_heat = !LATA5;

		// Reset timer flag
		TMR2IF = 0;
	}
}

/*
 * Main entry point.
 */
void main(void) __naked {
	unsigned int millisx60=0;
	unsigned int ad_res=0;

	init();

	//Loop forever
	while (1) {


		if(TMR4IF){

			millisx60++;

			// Read and accumulate AD value
			ad_res += (ADRESH << 8) | ADRESL;

			// Start new conversion
			ADGO = 1;

			// Run every 16th time -> 16*60ms = 960ms
			// Close enough to 1s for our purposes.
			if((millisx60 & 0xf) == 0) {
				LATA0 = (ad_res >= 63488 || ad_res <= 2047);
				{
					unsigned char i;
					long temp = 32;

					// Interpolate between lookup table points
					for (i = 0; i < 64; i++) {
						if(((ad_res >> 3) & 0x3f) <= i) {
							temp += ad_lookup[((ad_res >> 9) & 0x1f)];
						} else {
							temp += ad_lookup[((ad_res >> 9) & 0x1f) + 1];
						}
					}
					ad_res = 0;
					// Divide by 64 to get back to normal temperature
					temperature = (temp >> 6);
				}

				temperature += eeprom_read_config(EEADR_TEMP_CORRECTION);

				if(eeprom_read_config(EEADR_POWER_ON) && !LATA0){ // Bypass regulation if power is 'off' or alarm

					// Update running profile every minute (if there is one)
					// and handle reset of millis counter
					if(((unsigned char)eeprom_read_config(EEADR_RUN_MODE)) < 6){
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
						temperature_to_led(temperature);
					}

				} else { // Power is 'off' or alarm, disable outputs
					if(LATA0){
						led_10 = 0x11; // A
						led_1 = 0xcb; //L
					} else {
						led_10 = led_1 = 0xff;
					}
					led_e.led_e = led_01 = 0xff;
					LATA4 = 0;
					LATA5 = 0;
				}

			} // End 1 sec section

			// Reset timer flag
			TMR4IF = 0;
		}

		if(TMR6IF) {

			// Handle button press and menu
			button_menu_fsm();

			// Reset timer flag
			TMR6IF = 0;
		}

		// Reset watchdog
		ClrWdt();
	}
}
