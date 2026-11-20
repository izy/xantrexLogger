/*
 Xantrex GT Inverter Logger with PVoutput.org integration
 v0.01 - 27.09.2012
   - initial development code
 v0.02 - 20.11.2026
   - fix PVoutput date/time format and check PVoutput's reply
   - safer inverter serial reads and parsing
   - don't upload until the clock has been set from NTP, and check NTP replies
   - keep retrying DHCP instead of stopping, and renew the DHCP lease
   - needs the TimeLib version of the Time library (see README.txt)
 
 This sketch uses DHCP to obtain an IP address,
 gets the current time from an NTP server, polls a Xantrex GT inverter
 via serial port and logs the relevant statistics to PVoutput.org
  
 Circuit:
 - Arduino with a W5100 Ethernet shield, with no card in the shield's SD slot
 - inverter RS-232 port -> RS-232 to TTL converter (e.g. MAX232) -> pins 4 (RX) and 5 (TX)
   never connect the inverter's RS-232 port straight to the Arduino, see README.txt
 
 by Isaac Hauser (izy@zooked.com.au)
 */
 
#include <SPI.h>         
#include <Ethernet.h>
#include <TimeLib.h>
#include <SoftwareSerial.h>

// set the MAC address for our controller below.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // must be unique on local network
char timeServer[] = "au.pool.ntp.org"; // NTP server
const long timeZoneOffset = +36000L; // time offset in seconds (no daylight saving adjustment)
unsigned int ntpSyncTime = 3600; // how often to sync to NTP server in seconds
unsigned int ntpRetryTime = 60; // how long to wait after a failed sync in seconds

// NTP variables, don't modify
unsigned int localPort = 8888;
const int NTP_PACKET_SIZE= 48;
byte packetBuffer[NTP_PACKET_SIZE]; 
EthernetUDP Udp;
unsigned long ntpLastAttempt = 0; // millis() when we last tried to sync
unsigned int ntpWaitTime = 0; // seconds to wait after ntpLastAttempt before syncing again
time_t prevDisplay = 0;

// PVoutput variables, please set your API key and System ID
char serverName[] = "www.pvoutput.org";
char apiKey[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxx"; // insert your own apiKey here
char sid[] = "xxxxx"; // insert your own system ID here
unsigned int PVoutputUpdateTime = 300; // how often to send updates to PVoutput in seconds
unsigned int PVoutputOfflineTime = 60; // how often to check an offline inverter in seconds

// PVoutput variables, don't modify
unsigned long PVoutputLastCheck = 0; // millis() when we last checked the inverter
unsigned int PVoutputWaitTime = 0; // seconds to wait after PVoutputLastCheck, 0 so the first check happens straight away
const unsigned long HTTP_TIMEOUT = 5000; // how long to wait for PVoutput to reply in milliseconds
// results from updatePVoutput()
#define PVOUTPUT_OK 0
#define PVOUTPUT_NO_CONNECTION 1 // couldn't reach the server, worth trying again
#define PVOUTPUT_ERROR 2 // no inverter data, or the server rejected or didn't answer the update

// collects the HTTP request so it can be sent to PVoutput in one go, because the Ethernet
// library sends every write() as its own network packet and print(F(...)) writes one character at a time
class RequestBuffer : public Print {
public:
  char text[256];
  size_t length;
  boolean overflow;
  RequestBuffer() : length(0), overflow(false) {}
  size_t write(uint8_t c) {
    if (length >= sizeof(text)) {
      overflow = true;
      return 0;
    }
    text[length++] = c;
    return 1;
  }
};

// initialise the Ethernet client library
EthernetClient client;

// initialise our software serial connection
// pin 4 is also the SD card select on the Ethernet shield, so leave the SD slot empty
// or move rxPin and txPin to free pins (e.g. 6 and 7)
#define rxPin 4
#define txPin 5
SoftwareSerial xantrexSerial(rxPin,txPin);
// set max number of times to try getting a useful value from the inverter
const int MAXTRIES = 20;
// how long to wait for the inverter to reply in milliseconds
const unsigned long INVERTER_TIMEOUT = 200;
// longest reply we keep from the inverter
const int REPLY_SIZE = 32;


// SETUP
void setup() {
  xantrexSerial.begin(9600);
  Serial.begin(9600);
  welcomeBanner();
  startEthernet();
  Udp.begin(localPort);
  
  // try to get and set the date and time
  syncTime();
}


// LOOP
void loop() {
  // renew the DHCP lease when it is due
  Ethernet.maintain();

  // check if time needs to be synced to NTP server
  if(millis() - ntpLastAttempt >= ntpWaitTime * 1000UL) {
    syncTime();
  }
  
  // check if it's time to update PVoutput
  if(millis() - PVoutputLastCheck >= PVoutputWaitTime * 1000UL) {
    PVoutputLastCheck = millis();
    if (timeStatus() == timeNotSet) {
      // without the time PVoutput would get the wrong date, so wait for NTP
      Serial.println(F("Time not set, update not possible"));
      PVoutputWaitTime = PVoutputOfflineTime;
    // if inverter is online, then do pvoutput update, else no update needed
    } else if (get_status() == 1) {
      PVoutputWaitTime = PVoutputUpdateTime; // if the inverter is online we go back to the normal update time
      // only try again if we couldn't reach the server, PVoutput limits how many requests we can make
      int result = PVOUTPUT_NO_CONNECTION;
      for (int trys = 0; trys < 10 && result == PVOUTPUT_NO_CONNECTION; trys++) {
        result = updatePVoutput();
      }
      if (result == PVOUTPUT_OK) {
        Serial.println(F("PVoutput update success"));
      } else {
        Serial.println(F("PVoutput update failed"));
      }
    } else {
      Serial.println(F("Inverter offline, update not required"));
      PVoutputWaitTime = PVoutputOfflineTime; // if the inverter isn't online, we check again sooner
    }
  }


  // Display the time if it has changed by more than a minute.
  if(now() - prevDisplay >= 60) {
    prevDisplay = now();
    clockDisplay();
  }
}


// FUNCTIONS
// display welcome banner
void welcomeBanner() {
  Serial.println(F("Xantrex Logger - v0.02"));
  Serial.println(F("By Isaac Hauser (izy@zooked.com.au)"));
  Serial.println();
}

// start ethernet communications
void startEthernet() {
  client.stop();
  Serial.println(F("Connecting to network..."));
  
  // Connect to network and obtain an IP address using DHCP,
  // keep trying because the router may still be starting up after a power cut
  while (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP Failed, trying again in 10 seconds"));
    delay(10000);
  }
  Serial.println(F("Connected to network using DHCP"));
  Serial.print(F("IP Address: "));
  Serial.println(Ethernet.localIP());
  Serial.print(F("Subnet Mask: "));
  Serial.println(Ethernet.subnetMask());
  Serial.print(F("Gateway Address: "));
  Serial.println(Ethernet.gatewayIP());
  Serial.print(F("DNS Server Address: "));
  Serial.println(Ethernet.dnsServerIP());
  Serial.println();
  delay(1000);
}


// read the inverter and send its stats to PVoutput.org
// returns PVOUTPUT_OK, PVOUTPUT_NO_CONNECTION or PVOUTPUT_ERROR
int updatePVoutput() {
  // read the inverter before connecting, so the server isn't kept waiting
  time_t t = now();
  long whToday;
  int pout;
  float temp;
  float voltage;
  boolean haveWhToday = get_whtoday(whToday);
  boolean havePout = get_pout(pout);
  boolean haveTemp = get_temp(temp);
  boolean haveVoltage = get_voltage(voltage);
    
  Serial.println(F("-----INVERTER STATS-----"));
  Serial.print(F("WH TODAY: "));
  if (haveWhToday) Serial.println(whToday); else Serial.println(F("no reading"));
  Serial.print(F("POUT: "));
  if (havePout) Serial.println(pout); else Serial.println(F("no reading"));
  Serial.print(F("TEMP: "));
  if (haveTemp) Serial.println(temp); else Serial.println(F("no reading"));
  Serial.print(F("VOLTAGE: "));
  if (haveVoltage) Serial.println(voltage); else Serial.println(F("no reading"));
  Serial.println(F("----------------------"));
    
  // PVoutput needs at least the energy or the power
  if (!haveWhToday && !havePout) {
    Serial.println(F("No energy or power reading from inverter"));
    return PVOUTPUT_ERROR;
  }
  
  RequestBuffer request;
  request.print(F("GET /service/r2/addstatus.jsp?key="));
  request.print(apiKey);
  request.print(F("&sid="));
  request.print(sid);

  // send date as yyyymmdd
  request.print(F("&d="));
  request.print(year(t));
  printTwoDigits(request, month(t));
  printTwoDigits(request, day(t));

  // send time as hh:mm
  request.print(F("&t="));
  printTwoDigits(request, hour(t));
  request.print(':');
  printTwoDigits(request, minute(t));

  // send Energy Generation in watt hours, example "10000"
  if (haveWhToday) {
    request.print(F("&v1="));
    request.print(whToday);
  }

  // send Power Generation in watts, example "1000"
  if (havePout) {
    request.print(F("&v2="));
    request.print(pout);
  }

  // send Temperature in celsius, example "23.4"
  if (haveTemp) {
    request.print(F("&v5="));
    request.print(temp);
  }

  // send Voltage in volts, example "240.0"
  if (haveVoltage) {
    request.print(F("&v6="));
    request.print(voltage);
  }

  // finish HTTP GET request
  request.println(F(" HTTP/1.0"));
  request.print(F("Host: "));
  request.println(serverName);
  request.println();
  if (request.overflow) {
    Serial.println(F("request too long"));
    return PVOUTPUT_ERROR;
  }

  // connect() returns 1 when connected, and 0 or a negative number (e.g. a failed DNS lookup) when not
  if (client.connect(serverName, 80) != 1) {
    Serial.println(F("connection failed"));
    client.stop();
    return PVOUTPUT_NO_CONNECTION;
  }
  client.write((const uint8_t *)request.text, request.length);

  // the first line of the reply is the status, e.g. "HTTP/1.1 200 OK"
  char status[32];
  client.setTimeout(HTTP_TIMEOUT);
  int len = client.readBytesUntil('\n', status, sizeof(status) - 1);
  status[len] = '\0';
  if (len == 0) {
    // don't send it again, the server may have got the update even though it didn't reply
    Serial.println(F("no reply from server"));
    client.stop();
    return PVOUTPUT_ERROR;
  }
  int result = strstr(status, " 200") != NULL ? PVOUTPUT_OK : PVOUTPUT_ERROR;
  Serial.println(status);

  // print the rest of the reply from the server
  unsigned long start = millis();
  while ((client.connected() || client.available()) && millis() - start < HTTP_TIMEOUT) {
    if (client.available()) {
      char c = client.read();
      Serial.print(c);
    }
  }
  Serial.println();
  Serial.println();
  Serial.println(F("disconnecting."));
  client.stop();
  return result;
}


// try to get the date and time from the NTP server, and schedule the next sync
void syncTime() {
  ntpLastAttempt = millis();
  for (int trys = 0; trys < 10; trys++) {
    if (getTimeAndDate()) {
      Serial.println(F("ntp server update success"));
      ntpWaitTime = ntpSyncTime;
      return;
    }
  }
  Serial.println(F("ntp server update failed"));
  ntpWaitTime = ntpRetryTime; // try again sooner
}


// ask the NTP server for the time, returns true if the clock was set
boolean getTimeAndDate() {
  // throw away any old replies we didn't read
  while (Udp.parsePacket() > 0) {
  }
  if (!sendNTPpacket(timeServer)) {
    return false;
  }
  // wait up to 1.5 seconds for the reply
  unsigned long start = millis();
  while (millis() - start < 1500) {
    // anything shorter than a full NTP packet, or not from the NTP port, isn't a reply,
    // and is thrown away by the next parsePacket()
    if (Udp.parsePacket() >= NTP_PACKET_SIZE && Udp.remotePort() == 123) {
      Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer
      // only trust a reply from a server (mode 4) that is synchronised (stratum 1 to 15)
      byte mode = packetBuffer[0] & 0x07;
      byte stratum = packetBuffer[1];
      // the transmit timestamp starts at byte 40, it is seconds since 1 Jan 1900
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      if (mode == 4 && stratum >= 1 && stratum <= 15 && secsSince1900 != 0) {
        setTime(secsSince1900 - 2208988800UL + timeZoneOffset);
        return true;
      }
      // not a usable reply, keep waiting rather than asking the server again straight away
    }
  }
  return false;
}


// send an NTP request to the time server, returns false if it couldn't be sent
boolean sendNTPpacket(const char *address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;                  
  // beginPacket() returns 1 when ready, and 0 or a negative number (e.g. a failed DNS lookup) when not
  if (Udp.beginPacket(address, 123) != 1) {
    return false;
  }
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  return Udp.endPacket() == 1;
}
 
 
// clock display of the time and date (basic)
void clockDisplay() {
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(' ');
  Serial.print(day());
  Serial.print(' ');
  Serial.print(month());
  Serial.print(' ');
  Serial.print(year());
  Serial.println();
}
 
 
// utility function for clock display: prints preceding colon and leading 0
void printDigits(int digits) {
  Serial.print(':');
  printTwoDigits(Serial, digits);
}


// prints a number with a leading 0 if it is less than 10
void printTwoDigits(Print &out, int value) {
  if (value < 10) {
    out.print('0');
  }
  out.print(value);
}


// send a command to the inverter and read the first line of its reply
// returns false if the inverter didn't reply in time
boolean queryInverter(const __FlashStringHelper *command, char *reply, int size) {
  // throw away anything left over from an earlier command
  while (xantrexSerial.available() > 0) {
    xantrexSerial.read();
  }
  xantrexSerial.println(command);
    
  int count = 0;
  unsigned long start = millis();
  while (millis() - start < INVERTER_TIMEOUT) {
    if (xantrexSerial.available() > 0) {
      char c = xantrexSerial.read();
      if (c == '\r' || c == '\n') {
        if (count > 0) {
          break; // end of the reply
        }
      } else if (count < size - 1) {
        reply[count++] = c;
      }
    }
  }
  reply[count] = '\0';
  return count > 0;
}


// read a number from an inverter reply, skipping prefix (e.g. "C:") if the reply starts with it
// returns false if the reply doesn't contain a number
boolean parseNumber(const char *reply, const char *prefix, float &value) {
  if (prefix != NULL && strncmp(reply, prefix, strlen(prefix)) == 0) {
    reply += strlen(prefix);
  }
  char *end;
  float number = strtod(reply, &end);
  if (end == reply) {
    return false;
  }
  value = number;
  return true;
}


// keep asking the inverter until it replies with a number
// returns false if it doesn't after MAXTRIES attempts
boolean queryNumber(const __FlashStringHelper *command, const char *prefix, float &value) {
  char reply[REPLY_SIZE];
  for (int trys = 0; trys < MAXTRIES; trys++) {
    if (queryInverter(command, reply, sizeof(reply)) && parseNumber(reply, prefix, value)) {
      return true;
    }
    // if we get down here, the inverter gave us rubbish, repeat till we get useful data
  }
  return false;
}


// gets power output of inverter in watts, the inverter replies e.g. "1234"
boolean get_pout(int &pout) {
  float watts;
  if (queryNumber(F("POUT?"), NULL, watts) && watts >= 0) {
    pout = (int)(watts + 0.5);
    return true;
  }
  return false;
}


// gets temperature of inverter in degrees C, the inverter replies e.g. "C:34.2 F:93.6"
boolean get_temp(float &temp) {
  return queryNumber(F("MEASTEMP?"), "C:", temp);
}


// gets energy generated today in watt hours, the inverter replies in kWh e.g. "5.123"
// a long is needed because an int can only hold up to 32767 Wh
boolean get_whtoday(long &whtoday) {
  float kwh;
  if (queryNumber(F("KWHTODAY?"), NULL, kwh) && kwh >= 0) {
    whtoday = (long)(kwh * 1000 + 0.5);
    return true;
  }
  return false;
}


// check if inverter relay is on or off which should tell us if inverter is online
// returns 1 if online, 0 if offline, or -1 if the inverter didn't give a useful reply
int get_status()
{
  char reply[REPLY_SIZE];
  for (int trys = 0; trys < MAXTRIES; trys++) {
    if (queryInverter(F("RELAY?"), reply, sizeof(reply))) {
      if (strncmp(reply, "OFF", 3) == 0) {
        return 0;
      }
      if (strncmp(reply, "ON", 2) == 0) {
        return 1;
      }
    }
    // if we get down here, the inverter gave us rubbish, repeat till we get useful data
  }
  return -1;
}


// gets ac voltage of inverter, the inverter replies e.g. "240.5"
boolean get_voltage(float &voltage) {
  return queryNumber(F("VOUT?"), NULL, voltage);
}
