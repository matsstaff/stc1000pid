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

#ifndef __STC1000PI_H__
#define __STC1000PI_H__

/* Define STC-1000+ version number (XYY, X=major, YY=minor) */
/* Also, keep track of last version that has changes in EEPROM layout */
#define STC1000PI_VERSION			100
#define STC1000PI_EEPROM_VERSION	10

#define abs(x)	((x) < 0 ? -(x) : (x))

/* Define limits for temperatures */
#ifdef FAHRENHEIT
#define TEMP_MAX		(2500)
#define TEMP_MIN		(-400)
#define TEMP_CORR_MAX	(50)
#define TEMP_CORR_MIN	(-50)
#else  // CELSIUS
#define TEMP_MAX		(1400)
#define TEMP_MIN		(-400)
#define TEMP_CORR_MAX	(25)
#define TEMP_CORR_MIN	(-25)
#endif

enum set_options {
	SET_OPT_TEMP_CORRECTION = 0,
	SET_OPT_SETPOINT,
	SET_OPT_CURRENT_STEP,
	SET_OPT_CURRENT_STEP_DURATION,
	SET_OPT_PERIOD,
	SET_OPT_KP,
	SET_OPT_KI,
//	SET_OPT_KD,
	SET_OPT_OUTPUT,
	SET_OPT_MIN_OUTPUT,
	SET_OPT_MAX_OUTPUT,
//	SET_OPT_RAMPING,
	SET_OPT_RUN_MODE,
	SET_OPT_POWER_ON
};

/* Defines for EEPROM config addresses */
#define EEADR_PROFILE_SETPOINT(profile, step)	((profile)*19 + (step)*2)
#define EEADR_PROFILE_DURATION(profile, step)	((profile)*19 + (step)*2 + 1)
#define EEADR_TEMP_CORRECTION					(114 + SET_OPT_TEMP_CORRECTION)
#define EEADR_SETPOINT							(114 + SET_OPT_SETPOINT)
#define EEADR_CURRENT_STEP						(114 + SET_OPT_CURRENT_STEP)
#define EEADR_CURRENT_STEP_DURATION				(114 + SET_OPT_CURRENT_STEP_DURATION)
#define EEADR_PERIOD							(114 + SET_OPT_PERIOD)
#define EEADR_KP								(114 + SET_OPT_KP)
#define EEADR_KI								(114 + SET_OPT_KI)
//#define EEADR_KD								(114 + SET_OPT_KD)
#define EEADR_OUTPUT							(114 + SET_OPT_OUTPUT)
#define EEADR_MIN_OUTPUT						(114 + SET_OPT_MIN_OUTPUT)
#define EEADR_MAX_OUTPUT						(114 + SET_OPT_MAX_OUTPUT)
//#define EEADR_RAMPING							(114 + SET_OPT_RAMPING)
#define EEADR_RUN_MODE							(114 + SET_OPT_RUN_MODE)
#define EEADR_POWER_ON							(114 + SET_OPT_POWER_ON)

/* Declare functions and variables from Page 0 */

typedef union
{
	unsigned char led_e;

	struct
	  {
	  unsigned                      : 1;
	  unsigned e_point              : 1;
	  unsigned e_c                  : 1;
	  unsigned e_heat               : 1;
	  unsigned e_negative           : 1;
	  unsigned e_deg                : 1;
	  unsigned e_set                : 1;
	  unsigned e_cool               : 1;
	  };
} _led_e_bits;

extern _led_e_bits led_e;
extern unsigned char led_10, led_1, led_01;
extern unsigned const char led_lookup[16];

extern unsigned int eeprom_read_config(unsigned char eeprom_address);
extern void eeprom_write_config(unsigned char eeprom_address,unsigned int data);
extern void value_to_led(int value, unsigned char decimal);
extern void update_period(unsigned char period);

#define int_to_led(v)			value_to_led(v, 0);
#define temperature_to_led(v)	value_to_led(v, 1);

/* Declare functions and variables from Page 1 */
extern void button_menu_fsm();

#endif // __STC1000PI_H__
