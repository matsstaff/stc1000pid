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

#define reset() { __asm RESET __endasm; }

/* Helpful defines to handle buttons */
#define BTN_PWR				0x88
#define BTN_S				0x44
#define BTN_UP				0x22
#define BTN_DOWN			0x11

#define BTN_IDLE(btn)			((_buttons & (btn)) == 0x00)
#define BTN_PRESSED(btn)		((_buttons & (btn)) == ((btn) & 0x0f))
#define BTN_HELD(btn)			((_buttons & (btn)) == (btn))
#define BTN_RELEASED(btn)		((_buttons & (btn)) == ((btn) & 0xf0))
#define BTN_HELD_OR_RELEASED(btn)	((_buttons & (btn) & 0xf0))

static unsigned int divu10(unsigned int n) {
	unsigned int q, r;
	q = (n >> 1) + (n >> 2);
	q = q + (q >> 4);
	q = q + (q >> 8);
	q = q >> 3;
	r = n - ((q << 3) + (q << 1));
	return q + ((r + 6) >> 4);
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



extern unsigned char peak_count;
extern int base_temperature;
extern unsigned int peaks[];
extern int peaks_t[];

extern unsigned char at_state;
extern unsigned char output_start;
extern unsigned char output_swing;
extern unsigned char hyst;


/* Set menu struct */
struct s_setmenu {
    unsigned char led_c_10;
    unsigned char led_c_1;
    unsigned char led_c_01;
    unsigned char min;
    unsigned char max;
};

struct s_setmenu setmenu[] = {
	{ LED_O, LED_S, LED_OFF, 0, 255 },
	{ LED_O, LED_d, LED_OFF, 0, 255 },
	{ LED_h, LED_y, LED_OFF, 0, 20 },
	{ LED_P, LED_d, LED_OFF, 0, 4 }
};

#define MENU_SIZE (sizeof(setmenu) / sizeof(setmenu[0]))

/* States for the menu FSM */
enum menu_states {
	state_idle = 0,

	state_show_menu_item,
	state_set_menu_item,

	state_show_value,
	state_set_value,

	state_show_end_prg,
	state_end_prg
};

static unsigned char state=state_idle;
static unsigned char menu_item=0, countdown=0, item=0;
static unsigned char _buttons = 0;
void button_menu_fsm(){
	{
		unsigned char trisc, latb;

		// Disable interrups while reading buttons
		GIE = 0;

		// Save registers that interferes with LED's
		latb = LATB;
		trisc = TRISC;

		LATB = 0b00000000; // Turn off LED's
		TRISC = 0b11011000; // Enable input for buttons

		_buttons = (_buttons << 1) | RC7; // pwr
		_buttons = (_buttons << 1) | RC4; // s
		_buttons = (_buttons << 1) | RC6; // up
		_buttons = (_buttons << 1) | RC3; // down

		// Restore registers
		LATB = latb;
		TRISC = trisc;

		// Reenable interrups
		GIE = 1;
	}

	if(countdown){
		countdown--;
	}

	switch(state){
		case state_idle:
			if (BTN_RELEASED(BTN_S)) {
				if(at_state) {
					state = state_show_end_prg;
				} else {
					state = state_show_menu_item;
				}
			}
			if (BTN_HELD(BTN_UP)) {
				int_to_led(peaks_t[0]);
			}
			if (BTN_HELD(BTN_DOWN)) {
				int_to_led(peaks_t[1]);
			}
			if (BTN_HELD(BTN_PWR)) {
				int_to_led(base_temperature);
			}

			break;
		case state_show_menu_item:
			led_e.e_negative = 1;
			led_e.e_deg = 1;
			led_e.e_c = 1;
			if(menu_item < MENU_SIZE){
				led_10.raw = setmenu[menu_item].led_c_10;
				led_1.raw = setmenu[menu_item].led_c_1;
				led_01.raw = setmenu[menu_item].led_c_01;
			} else {
				led_10.raw = LED_r;
				led_1.raw = LED_n;
				led_01.raw = LED_OFF;
			}
			countdown = 200;
			state = state_set_menu_item;
			break;
		case state_set_menu_item:
			if(countdown==0){
				state = state_idle;
			} else if(BTN_RELEASED(BTN_PWR)){
				state = state_idle;
			} else if(BTN_RELEASED(BTN_UP)){
				if(menu_item >= MENU_SIZE){
					menu_item=0;
				} else {
					menu_item++;
				}
				state = state_show_menu_item;
			} else if(BTN_RELEASED(BTN_DOWN)){
				if(menu_item <= 0){
					menu_item=MENU_SIZE;
				} else {
					menu_item--;
				}
				state = state_show_menu_item;
			} else if(BTN_RELEASED(BTN_S)){
				state = state_show_value;
				if(menu_item == 0){
					item = output_start;
				} else if(menu_item == 1){
					item = output_swing;
				} else if(menu_item == 2){
					item = hyst;
				} else if(menu_item == 3){
					item = eeprom_read_config(EEADR_SET_MENU_ITEM(Pd));
				} else {
					at_state = 1;
					state = state_idle;
				}
			}
			break;
		case state_show_value:
			if(menu_item == 2){
				temperature_to_led(item);
			} else {
				int_to_led(item);
			}
			countdown = 200;
			state = state_set_value;
			break;
		case state_set_value:
			if(countdown==0){
				state=state_idle;
			} else if(BTN_RELEASED(BTN_PWR)){
				state = state_show_menu_item;
			} else if(BTN_HELD_OR_RELEASED(BTN_UP)) {
				if(item>=setmenu[menu_item].max){
					item = setmenu[menu_item].min;
				} else {
					item++;
				}
				state = state_show_value;
			} else if(BTN_HELD_OR_RELEASED(BTN_DOWN)) {
				if(item<=setmenu[menu_item].min){
					item = setmenu[menu_item].max;
				} else {
					item--;
				}
				state = state_show_value;
			} else if(BTN_RELEASED(BTN_S)){
				if(menu_item == 0){
					output_start = item;
				} else if(menu_item == 1){
					output_swing = item;
				} else if(menu_item == 2){
					hyst = item;
				} else if(menu_item == 3){
					eeprom_write_config(EEADR_SET_MENU_ITEM(Pd), item);
					update_period((unsigned char)item);
				}
				state = state_show_menu_item;
			}
			break;
		case state_show_end_prg:
			led_e.e_negative = 1;
			led_e.e_deg = 1;
			led_e.e_c = 1;
			led_10.raw = LED_E;
			led_1.raw = LED_n;
			led_01.raw = LED_d;
			countdown = 200;
			state = state_end_prg;
			break;
		case state_end_prg:
			if(countdown == 0){
				state = state_idle;
			} else if(BTN_RELEASED(BTN_S)){
				at_state = 0;
				led_e.e_point = 1;
				state = state_idle;
			}
			break;		
		default:
			state=state_idle;
	} // end switch

	TMR1GE = (state==0);

}

