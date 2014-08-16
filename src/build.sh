#/bin/bash

make all clean

v=`cat stc1000pi.h | grep STC1000PI_VERSION`
e=`cat stc1000pi.h | grep STC1000PI_EEPROM_VERSION`

cat picprog.ino | sed -n '/^const char hex_celsius\[\] PROGMEM/q;p' | sed "s/^#define STC1000PI_VERSION.*/$v/" | sed "s/^#define STC1000PI_EEPROM_REV.*/$e/" >> picprog.tmp

echo "const char hex_celsius[] PROGMEM = {" >> picprog.tmp; 
for l in `cat stc1000pi_celsius.hex | sed 's/^://' | sed 's/\(..\)/0\x\1\,/g'`; do 
	echo "   $l" | sed 's/0x00,0x00,0x00,0x01,0xFF,/0x00,0x00,0x00,0x01,0xFF/' >> picprog.tmp; 
done; 
echo "};" >> picprog.tmp

echo "const char hex_fahrenheit[] PROGMEM = {" >> picprog.tmp; 
for l in `cat stc1000pi_fahrenheit.hex | sed 's/^://' | sed 's/\(..\)/0\x\1\,/g'`; do 
	echo "   $l" | sed 's/0x00,0x00,0x00,0x01,0xFF,/0x00,0x00,0x00,0x01,0xFF/' >> picprog.tmp; 
done; 
echo "};" >> picprog.tmp

echo "const char hex_eeprom_celsius[] PROGMEM = {" >> picprog.tmp; 
for l in `cat eedata_celsius.hex | sed 's/^://' | sed 's/\(..\)/0\x\1\,/g'`; do 
	echo "   $l" | sed 's/0x00,0x00,0x00,0x01,0xFF,/0x00,0x00,0x00,0x01,0xFF/' >> picprog.tmp; 
done; 
echo "};" >> picprog.tmp

echo "const char hex_eeprom_fahrenheit[] PROGMEM = {" >> picprog.tmp; 
for l in `cat eedata_fahrenheit.hex | sed 's/^://' | sed 's/\(..\)/0\x\1\,/g'`; do 
	echo "   $l" | sed 's/0x00,0x00,0x00,0x01,0xFF,/0x00,0x00,0x00,0x01,0xFF/' >> picprog.tmp; 
done; 
echo "};" >> picprog.tmp

mv -f picprog.ino picprog.bkp
mv picprog.tmp picprog.ino
