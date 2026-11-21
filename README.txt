Xantrex GT Inverter Logger with PVoutput.org integration
v0.02 - 20.11.2026
- bug fixes, see the notes at the top of xantrexLogger.ino
v0.01 - 27.09.2012
- initial development code release

This sketch uses DHCP to obtain an IP address,
gets the current time from an NTP server, polls a Xantrex GT inverter
via serial port and logs the relevant statistics to PVoutput.org

Hardware:
- Arduino Uno (or compatible) with a W5100 based Ethernet shield
- RS-232 to TTL converter (MAX232 or MAX3232 module)
- serial cable to the inverter's RS-232 port

Wiring:
- The inverter's serial port uses RS-232 voltage levels (up to +/-12V).
  Never connect it straight to the Arduino, it can damage the Arduino.
  Go through the RS-232 to TTL converter:
    data from the inverter (converter TTL output) -> Arduino pin 4 (RX)
    data to the inverter (converter TTL input)    -> Arduino pin 5 (TX)
    converter GND                                 -> Arduino GND
- The serial link runs at 9600 baud.
- Pin 4 is also the SD card chip select on the Arduino Ethernet shield,
  so leave the shield's SD card slot empty. If you need the SD slot,
  move the inverter to free pins (e.g. 6 and 7) and change rxPin and
  txPin in xantrexLogger.ino to match.

Requires:
- Arduino IDE (or arduino-cli) with the Arduino AVR Boards core
- SPI.h and SoftwareSerial.h (these come with the AVR core)
- Ethernet library ("Ethernet" in the Library Manager, if your IDE doesn't already have it)
- Time library: install "Time" by Michael Margolis (maintained by Paul Stoffregen)
  from the Library Manager. The sketch includes <TimeLib.h>.
- Time.zip and SoftwareSerial.zip are the 2012 copies for Arduino 1.0.1,
  they aren't needed with a current IDE

Setup:
- set apiKey and sid in xantrexLogger.ino to your PVoutput API key and system ID
- set mac to a MAC address that is unique on your network
- timeZoneOffset is +10 hours (AEST) with no daylight saving adjustment,
  change it if you are in a different time zone
- the W5100 can't do HTTPS, so updates are sent to PVoutput over plain HTTP
