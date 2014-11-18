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
#define BTN_PWR			0x88
#define BTN_S			0x44
#define BTN_UP			0x22
#define BTN_DOWN		0x11

#define BTN_IDLE(btn)				((_buttons & (btn)) == 0x00)
#define BTN_PRESSED(btn)			((_buttons & (btn)) == ((btn) & 0x0f))
#define BTN_HELD(btn)				((_buttons & (btn)) == (btn))
#define BTN_RELEASED(btn)			((_buttons & (btn)) == ((btn) & 0xf0))
#define BTN_HELD_OR_RELEASED(btn)	((_buttons & (btn) & 0xf0))

#if 0

/* Help to convert menu item number and config item number to an EEPROM config address */
#define EEADR_MENU_ITEM(mi, ci)	((mi)*19 + (ci))

/* Set menu struct */
struct s_setmenu {
    unsigned char led_c_10;
    unsigned char led_c_1;
    unsigned char led_c_01;
    int min;
    int max;
};

/* Set menu struct data generator */
#define TO_STRUCT(name, led10ch, led1ch, led01ch, minv, maxv, dvc, dvf) \
    { led10ch, led1ch, led01ch, minv, maxv },

static const struct s_setmenu setmenu[] = {
	SET_MENU_DATA(TO_STRUCT)
};

/* Helpers to constrain user input  */
static int RANGE(int x, int min, int max){
	if(x>max)
		return min;
	if(x<min)
		return max;
	return x;
}

/* Check and constrain a configuration value */
static int check_config_value(int config_value, unsigned char eeadr){
	if(eeadr < EEADR_SET_MENU){
		if(eeadr & 0x1){
			config_value = RANGE(config_value, 0, 999);
		} else {
			config_value = RANGE(config_value, TEMP_MIN, TEMP_MAX);
		}
	} else {
		eeadr -= EEADR_SET_MENU;
		config_value = RANGE(config_value, setmenu[eeadr].min, setmenu[eeadr].max);
	}
	return config_value;
}

static void prx_to_led(unsigned char run_mode, unsigned char is_menu){
	led_e.e_negative = 1;
	led_e.e_deg = 1;
	led_e.e_c = 1;
	led_e.e_point = 1;
	if(run_mode<6){
		led_10.raw = LED_P;
		led_1.raw = LED_r;
		led_01.raw = led_lookup[run_mode];
	} else {
		if(is_menu){
			led_10.raw = LED_S;
			led_1.raw = LED_e;
			led_01.raw = LED_t;
		} else {
			led_10.raw = LED_c;
			led_01.raw = LED_OFF;
			if(run_mode<7){
				led_1.raw = LED_t;
			} else {
				led_1.raw = LED_O;
			}
		}
	}
}

#define run_mode_to_led(x)	prx_to_led(x,0)
#define menu_to_led(x)	prx_to_led(x,1)

/* States for the menu FSM */
enum menu_states {
	state_idle = 0,

	state_power_down_wait,

	state_show_version,

	state_show_sp,

	state_show_profile,
	state_show_profile_st,
	state_show_profile_dh,

	state_show_menu_item,
	state_set_menu_item,
	state_show_config_item,
	state_set_config_item,
	state_show_config_value,
	state_set_config_value,

	state_up_pressed,
	state_down_pressed,
};

/* Due to a fault in SDCC, static local variables are not initialized
 * properly, so the variables below were moved from button_menu_fsm()
 * and made global.
 */
static unsigned char state=state_idle;
static unsigned char menu_item=0, config_item=0, countdown=0;
static int config_value;
static unsigned char _buttons = 0;

/* This is the button input and menu handling function.
 * arguments: none
 * returns: nothing
 */
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
		if(BTN_PRESSED(BTN_PWR)){
			countdown = 50; // 3 sec
			state = state_power_down_wait;
		} else if(_buttons && eeprom_read_config(EEADR_POWER_ON)){
			if (BTN_PRESSED(BTN_UP | BTN_DOWN)) {
				state = state_show_version;
			} else if (BTN_PRESSED(BTN_UP)) {
				state = state_show_sp;
			} else if (BTN_PRESSED(BTN_DOWN)) {
				countdown = 25; // 1.5 sec
				state = state_show_profile;
			} else if (BTN_RELEASED(BTN_S)) {
				state = state_show_menu_item;
			}
		}
		break;

	case state_show_version:
		int_to_led(STC1000PI_VERSION);
		led_10.decimal = 0;
		led_e.e_deg = 1;
		led_e.e_c = 1;
		if(!BTN_HELD(BTN_UP | BTN_DOWN)){
			state=state_idle;
		}
		break;

	case state_power_down_wait:
		if(countdown==0){
			unsigned char pwr_on = eeprom_read_config(EEADR_POWER_ON);
			eeprom_write_config(EEADR_POWER_ON, !pwr_on);
			if(pwr_on){
				LATA0 = 0;
				LATA4 = 0;
				LATA5 = 0;
				TMR4ON = 0;
				TMR4IF = 0;
			} else {
				reset();
			}
			state = state_idle;
		} else if(!BTN_HELD(BTN_PWR)){
//			if((unsigned char)eeprom_read_config(EEADR_2ND_PROBE)){
					TX9 = !TX9;
//			}
			state = state_idle;
		}
		break;

	case state_show_sp:
		temperature_to_led(eeprom_read_config(EEADR_SET_MENU_ITEM(SP)));
		if(!BTN_HELD(BTN_UP)){
			state=state_idle;
		}
		break;

	case state_show_profile:
		{
			unsigned char run_mode = eeprom_read_config(EEADR_SET_MENU_ITEM(rn));
			run_mode_to_led(run_mode);
			if(run_mode<CONSTANT_TEMPERATURE_MODE && countdown==0){
				countdown=17;
				state = state_show_profile_st;
			}
			if(!BTN_HELD(BTN_DOWN)){
				state=state_idle;
			}
		}
		break;
	case state_show_profile_st:
		int_to_led(eeprom_read_config(EEADR_SET_MENU_ITEM(St)));
		if(countdown==0){
			countdown=25;
			state = state_show_profile_dh;
		}
		if(!BTN_HELD(BTN_DOWN)){
			state=state_idle;
		}
		break;
	case state_show_profile_dh:
		int_to_led(eeprom_read_config(EEADR_SET_MENU_ITEM(dh)));
		if(countdown==0){
			countdown=25;
			state = state_show_profile;
		}
		if(!BTN_HELD(BTN_DOWN)){
			state=state_idle;
		}
		break;

	case state_show_menu_item:
		menu_to_led(menu_item);
		countdown = 200;
		state = state_set_menu_item;
		break;
	case state_set_menu_item:
		if(countdown==0 || BTN_RELEASED(BTN_PWR)){
			state=state_idle;
		} else if(BTN_RELEASED(BTN_UP)){
			menu_item++;
			if(menu_item > SET_MENU_ITEM_NO){
				menu_item = 0;
			}
			state = state_show_menu_item;
		} else if(BTN_RELEASED(BTN_DOWN)){
			menu_item--;
			if(menu_item > SET_MENU_ITEM_NO){
				menu_item = SET_MENU_ITEM_NO;
			}
			state = state_show_menu_item;
		} else if(BTN_RELEASED(BTN_S)){
			config_item = 0;
			state = state_show_config_item;
		}
		break;
	case state_show_config_item:
		led_e.e_negative = 1;
		led_e.e_deg = 1;
		led_e.e_c = 1;
		if(menu_item < SET_MENU_ITEM_NO){
			if(config_item & 0x1) {
				led_10.raw = LED_d;
				led_1.raw = LED_h;
			} else {
				led_10.raw = LED_S;
				led_1.raw = LED_P;
			}
			led_01.raw = led_lookup[(config_item >> 1)];
		} else /* if(menu_item == 6) */{
			led_10.raw = setmenu[config_item].led_c_10;
			led_1.raw = setmenu[config_item].led_c_1;
			led_01.raw = setmenu[config_item].led_c_01;
		}
		countdown = 200;
		state = state_set_config_item;
		break;
	case state_set_config_item:
		if(countdown==0){
			state=state_idle;
		} else if(BTN_RELEASED(BTN_PWR)){
			state = state_show_menu_item;
		} else if(BTN_RELEASED(BTN_UP)){
			config_item++;
			if(menu_item < SET_MENU_ITEM_NO){
				if(config_item >= 19){
					config_item = 0;
				}
			} else {
				if(config_item >= SET_MENU_SIZE){
					config_item = 0;
				}
				/* Jump to exit code shared with BTN_DOWN case */
				/* GOTO's are frowned upon, but avoiding code duplication saves precious code space */
				goto chk_skip_menu_item;
			}
			state = state_show_config_item;
		} else if(BTN_RELEASED(BTN_DOWN)){
			config_item--;
			if(menu_item < SET_MENU_ITEM_NO){
				if(config_item > 18){
					config_item = 18;
				}
			} else {
				if(config_item > SET_MENU_SIZE-1){
					config_item = SET_MENU_SIZE-1;
				}
chk_skip_menu_item:
				if((unsigned char)eeprom_read_config(EEADR_SET_MENU_ITEM(rn)) >= CONSTANT_TEMPERATURE_MODE){
					if(config_item == St){
						config_item += 2;
					}else if(config_item == dh){
						config_item -= 2;
					}
				}
			}
			state = state_show_config_item;
		} else if(BTN_RELEASED(BTN_S)){
			unsigned char adr = EEADR_MENU_ITEM(menu_item, config_item);
			config_value = eeprom_read_config(adr);
			config_value = check_config_value(config_value, adr);
			countdown = 200;
			state = state_show_config_value;
		}
		break;
	case state_show_config_value:
		if(menu_item < SET_MENU_ITEM_NO){
			if(config_item & 0x1){
				int_to_led(config_value);
			} else {
				temperature_to_led(config_value);
			}
		} else /* if(menu_item == SET_MENU_ITEM_NO) */ {
			if(config_item <= SP){
				temperature_to_led(config_value);
			} else if (config_item < rn){
				int_to_led(config_value);
			} else {
				run_mode_to_led(config_value);
			}
		}
		countdown = 200;
		state = state_set_config_value;
		break;
	case state_set_config_value:
		{
			unsigned char adr = EEADR_MENU_ITEM(menu_item, config_item);

			if(countdown==0){
				state=state_idle;
			} else if(BTN_RELEASED(BTN_PWR)){
				state = state_show_config_item;
			} else if(BTN_HELD_OR_RELEASED(BTN_UP)) {
				config_value++;
				if(config_value > 1000){
					config_value+=9;
				}
				/* Jump to exit code shared with BTN_DOWN case */
				goto chk_cfg_acc_label;
			} else if(BTN_HELD_OR_RELEASED(BTN_DOWN)) {
				config_value--;
				if(config_value > 1000){
					config_value-=9;
				}
chk_cfg_acc_label:
				config_value = check_config_value(config_value, adr);
				if(PR6 > 30){
					PR6-=8;
				}
				state = state_show_config_value;
			} else if(BTN_RELEASED(BTN_S)){
				if(menu_item == SET_MENU_ITEM_NO){
					if(config_item == rn){
						// When setting runmode, clear current step & duration
						eeprom_write_config(EEADR_SET_MENU_ITEM(St), 0);
						eeprom_write_config(EEADR_SET_MENU_ITEM(dh), 0);
						if(config_value < CONSTANT_TEMPERATURE_MODE){
							unsigned char eeadr_sp = EEADR_PROFILE_SETPOINT(((unsigned char)config_value), 0);
							// Set intial value for SP
							eeprom_write_config(EEADR_SET_MENU_ITEM(SP), eeprom_read_config(eeadr_sp));
							// Hack in case inital step duration is '0'
							if(eeprom_read_config(eeadr_sp+1) == 0){
								config_value = CONSTANT_TEMPERATURE_MODE;
							}
						}
					} else if(config_item == Pd){
						update_period((unsigned char)config_value);
					}
				}
				eeprom_write_config(adr, config_value);
				state=state_show_config_item;
			} else {
				PR6 = 250;
			}
		}
		break;
	default:
		state=state_idle;
	}

	/* This is last resort...
	 * Start using unused registers for general purpose
	 * Use TMR1GE to flag if display should show temperature or not */
	TMR1GE = (state==0);

}
#endif

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
	{ LED_h, LED_y, LED_OFF, 0, 20 }
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

/*
	if(BTN_RELEASED(BTN_S)){
		TX9 = 1;
	}

	if(BTN_HELD(BTN_UP)){
		int a = peaks[2]-peaks[0];
		a = abs(a);
		int_to_led(a);
		TMR1GE = 0;
	} else if(BTN_HELD(BTN_DOWN)){
		int a = peaks[3]-peaks[1];
		a = abs(a);
		int_to_led(a);
		TMR1GE = 0;
	} else if(BTN_HELD(BTN_PWR)){
		//temperature_to_led(base_temperature);
		int_to_led(peak_count);
		TMR1GE = 0;
	} else {
		TMR1GE = 1;
	}
*/
}

