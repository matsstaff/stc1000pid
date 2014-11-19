STC\-1000+ (PID)
================

Improved firmware for mash control and Arduino based uploader for the STC-1000 dual stage thermostat.

![STC-1000](http://img.diytrade.com/cdimg/1066822/11467124/0/1261107339/temperature_controllers_STC-1000.jpg)

Features
--------
* Both Fahrenheit and Celsius versions
* PID control of heating output
* Selectable time period (1, 2, 4 or 8 secs)
* Up to 5 profiles with up to 10 setpoints.
* Each setpoint can be held for 1-999 minutes (i.e. up to ~16 hours).
* Somewhat intuitive menus for configuring
* Button acceleration, for frustrationless programming by buttons

PID firmware
============

Settings under the 'Set' menu:

|Setting|Description|
|-------|-----------|
|tc|Temperature correction probe 1|
|tc2|Temperature correction probe 2|
|SP|Setpoint|
|St|Current profile step|
|dh|Current profile duration (minutes)|
|cP|PID proportional gain|
|cI|PID integral gain|
|cD|PID derivative gain|
|OP|Output setting for constant output mode|
|OL|Lower output limit (for profile/constant temp mode)|
|OH|Upper output limit (for profile/constant temp mode)|
|Pb|Enable second probe (currently not in use)|
|rn|Select run mode|


Autotune
--------

The basic algorithm the autotune firmware uses:

1. Set output to 'OS' and blink LED
2. Wait until 4 temperature readings 1 minute apart differs by no more than 'hy'
3. If this takes longer than 30 minutes go to step 15 (fail)
4. Otherwise store the measured temperature as a variable (lets call it 'basetemp')
5. Set output to 'OS' - 'Od' (that is 'OS' minus 'Od') and turn LED off
6. Wait until temperature is less than 'basetemp' - 'hy' (again that is a minus)
7. Set output to 'OS' + 'Od' and turn LED on
8. Find lower 'peak' (i.e. minimum measured temp value), save that time and temp
9. Wait until temperature is greater than 'basetemp' + 'hy'
10. Set output to 'OS' - 'Od' (that is 'OS' minus 'Od') and turn LED off
11. Find upper 'peak' (i.e. maximum measured temp value), save that time and temp
12. If two consecutive periods (2 maxpeaks, 2 min peaks) are found, where the peak values differ by no more than hy then go to step 16 (success)
13. If more than 10 min-max attempts and no stable peak values are found go to step 15 (fail)
14. Otherwise repeat from step 6
15. Autotune failed, set output to 0, turn LED on and halt execution
16. Autotune succeeded, set output to 0, use the amplitude and period times to calculate cP, cI and cD settings (using Ziegler Nichols 'no overshoot'), store these directly to EEPROM, turn LED off and halt execution 
 
Settings in the autotune firmware:

|Setting|Description|
|-------|-----------|
|OS|Starting output|
|Od|Output differential|
|hy|Temperature hysteresis|
|rn|Start autotune|


Updates
=======
2014-11-18 Major rework. Added D (for full PID), added autotuning firmware, changed sketch (F/C selection manual) and reworked build script to match.

