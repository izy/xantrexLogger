Xantrex GT Inverter Logger with PVoutput.org intergration
v0.01 - 27.09.2012
- inital development code release
 
This sketch will ultimately use DHCP to obtain an IP address,
get the current time from an NTP server, poll a Xantrex GT inverter
via serial port and log the relevant statistics to PVoutput.org

Requires:
- Arduino 1.0.1
- SPI.h, Ethernet.h, SoftwareSerial.h (these all come with 1.0.1)
- Time.h needs to be imported from the zip file (if you can't work out how to do this, you should give up now)