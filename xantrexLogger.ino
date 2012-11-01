/*
 Xantrex GT Inverter Logger with PVoutput.org intergration
 v0.01 - 27.09.2012
   - inital development code
 
 This sketch will ultimately use DHCP to obtain an IP address,
 get the current time from an NTP server, poll a Xantrex GT inverter
 via serial port and log the relevant statistics to PVoutput.org
  
 Circuit:
 
 by Isaac Hauser (izy@zooked.com.au)
 */
 
#include <SPI.h>         
#include <Ethernet.h>
#include <Time.h>
#include <SoftwareSerial.h>

// set the MAC address for our controller below.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // must be unique on local network
IPAddress timeServer (203, 14, 0, 250); // tic.ntp.telstra.net
const long timeZoneOffset = +36000L; // time offset in seconds
unsigned int ntpSyncTime = 3600; // how oftern to sync to NTP server in seconds

// NTP variables, don't modify
unsigned int localPort = 8888;
const int NTP_PACKET_SIZE= 48;
byte packetBuffer[NTP_PACKET_SIZE]; 
EthernetUDP Udp;
unsigned long ntpLastUpdate = 0;
time_t prevDisplay = 0;

// PVoutput variables, please set your API key and System ID
char serverName[] = "www.pvoutput.org";
char apiKey[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxx"; // insert your own apiKey here
char sid[] = "xxxxx"; // insert your own system ID here
unsigned long PVoutputLastUpdate = 0;
unsigned int PVoutputUpdateTime = 300; // how oftern to send updates to PVoutput in seconds

// initialise the Ethernet client library
EthernetClient client;

// initialise our software serial connection
#define rxPin 4
#define txPin 5
SoftwareSerial xantrexSerial(rxPin,txPin);
// set max number of times to try getting a useful value from the inverter
int MAXTRIES = 20;


// SETUP
void setup() {
  pinMode(txPin, OUTPUT);
  pinMode(rxPin, INPUT);
  xantrexSerial.begin(9600);
  Serial.begin(9600);
  welcomeBanner();
  startEthernet();
  
  // try to get and set the date and time
  int trys = 0;
  while(!getTimeAndDate() && trys < 10) {
    trys++;
  } if (trys < 10) {
    Serial.println("ntp server update success");
  } else {
    Serial.println("ntp server updated failed");
  }
}


// LOOP
void loop() {
  // check if time needs to be synced to NTP server
  if(now()-ntpLastUpdate >= ntpSyncTime) {
    int trys = 0;
    while(!getTimeAndDate() && trys < 10){
      trys++;
    } if(trys < 10) {
      Serial.println("ntp server update success");
    } else {
      Serial.println("ntp server update failed");
    }
  }
  
  // check if it's time to update PVoutput
  if(now()-PVoutputLastUpdate >= PVoutputUpdateTime) {
    // if inverter is online, then do pvoutput update, else no update needed
    //if (1) {
    if (get_status() == 1) {
      PVoutputUpdateTime = 300; // if the inverter is online we go back to 300 seconds checks
      int trys = 0;
      while(!updatePVoutput() && trys < 10) {
        trys++;
      } if(trys < 10) {
        Serial.println("PVoutput update success");
      } else {
        Serial.println("PVoutput update failed");
      }
    } else {
      Serial.println("Inverter offline, update not required");
      PVoutputUpdateTime = 60; // if the inverter isn't online, we check again in 60 seconds
      PVoutputLastUpdate = now();
    }
  }


  // Display the time if it has changed by more than a minute.
  if(now()-60 >= prevDisplay) {
  //if(now() != prevDisplay) {
    prevDisplay = now();
    clockDisplay();
  }
}


// FUNCTIONS
// display welcome banner
void welcomeBanner() {
  Serial.println("Xantrex Logger - v0.01");
  Serial.println("By Isaac Hauser (izy@zooked.com.au)");
  Serial.println();
}

// start ethernet communications
void startEthernet() {
  client.stop();
  Serial.println("Connecting to network..."); 
  
  // Connect to network and obtain an IP address using DHCP
  if (Ethernet.begin(mac) == 0) {
    Serial.println("DHCP Failed, reset Arduino to try again");
    // no point in carrying on, so do nothing forevermore
    for(;;)
    ;
  } else {
    Serial.println("Connected to network using DHCP");
    Serial.print("IP Address: ");
    Serial.println(Ethernet.localIP());
    Serial.print("Subnet Mask: ");
    Serial.println(Ethernet.subnetMask());
    Serial.print("Gateway Address: ");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("DNS Server Address: ");
    Serial.println(Ethernet.dnsServerIP());
    Serial.println();
    delay(1000);
  }
}


// function for sending inverter info to PVoutput.org
int updatePVoutput() {
  int flag = 0;
  if(client.connect(serverName, 80))
  {
    client.print("GET /service/r2/addstatus.jsp?key=");
    client.print(apiKey);
    client.print("&sid=");
    client.print(sid);
    
    // send date
    client.print("&d=");
    client.print(year());
    client.print(month());
    client.print(day());
    
    // send time
    client.print("&t=");
    client.print(hour());
    client.print(":");
    client.print(minute());
    
    Serial.println("-----INVERTER STATS-----");
    
    // send Energy Generation in watt hours, example "10000"
    client.print("&v1=");    
    Serial.print("WH TODAY: ");
    int temp_wh = get_whtoday();
    Serial.println(temp_wh);
    client.print(temp_wh);
    
    // send Power Generation in watts, example "1000"
    client.print("&v2=");
    Serial.print("POUT: ");
    int temp_pout = get_pout();
    Serial.println(temp_pout);
    client.print(temp_pout);
    
    // send Temperature in celsius, example "23.4"
    client.print("&v5=");
    Serial.print("TEMP: ");
    float temp_temp = get_temp();
    Serial.println(temp_temp);
    client.print(temp_temp);
    
    // send Voltage in volts, example "240.0" 
    client.print("&v6=");
    Serial.print("VOLTAGE: ");
    float temp_voltage = get_voltage();
    Serial.println(temp_voltage);
    client.print(temp_voltage);
    
    Serial.println("----------------------");
    
    // finish HTTP GET string
    client.println(" HTTP/1.0");
    client.println();
    // wait a few seconds for server to response
    delay(3000);
    flag = 1;
    PVoutputLastUpdate = now();
  } 
  else
  {
    Serial.println("connection failed");
  }
  
  // if there are incoming bytes available 
  // from the server, read them and print them:
    while (client.available()) {
      char c = client.read();
      Serial.print(c);
    }
    Serial.println();
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
    return flag;
}


// do not alter this function, it is used by the system
int getTimeAndDate() {
  int flag = 0;
  Udp.begin(localPort);
  sendNTPpacket(timeServer);
  delay(1000);
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer
    unsigned long highWord, lowWord, epoch;
    highWord = word(packetBuffer[40], packetBuffer[41]);
    lowWord = word(packetBuffer[42], packetBuffer[43]);  
    epoch = highWord << 16 | lowWord;
    epoch = epoch - 2208988800 + timeZoneOffset;
    flag = 1;
    setTime(epoch);
    ntpLastUpdate = now();
  }
  return flag;
}


// do not alter this function, it is used by the system
unsigned long sendNTPpacket(IPAddress& address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;                  
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  Udp.endPacket();
}
 
 
// clock display of the time and date (basic)
void clockDisplay() {
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());
  Serial.println();
}
 
 
// utility function for clock display: prints preceding colon and leading 0
void printDigits(int digits) {
  Serial.print(":");
  if(digits < 10) {
    Serial.print('0');
  }
    Serial.print(digits);
}


// returns power output of inverter in watts
int get_pout()
{
  // loop till we get a good value
  int pout = 0;
  int trys = 0;
  while (pout == 0 && trys < MAXTRIES)
  {
    // send command to inverter
    xantrexSerial.println("POUT?");
    delay(50);
    
    // read response into array of chars
    char received[64];
    int count = 0;
    while (xantrexSerial.available() > 0 )
    {
      received[count++] = xantrexSerial.read();
    }
    
    // convert array of chars into a number we can use
    pout = atoi(received);
    trys++;
  }
  return pout;
}


// returns temperature of inverter in degress C
float get_temp()
{
  // loop till we get a good value
  float temp = 0;
  int trys = 0;
  while (temp == 0 && trys < MAXTRIES)
  {
    // send command to inverter
    xantrexSerial.println("MEASTEMP?");
    delay(50);
    
    // read response into array of chars
    char received[64];
    int count = 0;
    while (xantrexSerial.available() > 0 )
    {
      received[count++] = xantrexSerial.read();
    }
  
    // select only the bits of the recevied data we are interested in
    char tempArray[3] = {received[2],received[3],received[5]};
    // convert to a number we can use
    temp = atof(tempArray)/10;
    trys++;
  }
  return temp;
}


// returns energy generated today in watt hours
int get_whtoday()
{
  // loop till we get a good value
  int whtoday = 0;
  int trys = 0;
  while (whtoday == 0 && trys < MAXTRIES)
  {
    // send command to inverter
    xantrexSerial.println("KWHTODAY?");
    delay(50);
    
    // read response into array of chars
    char received[64];
    int count = 0;
    while (xantrexSerial.available() > 0 )
    {
      received[count++] = xantrexSerial.read();
    }
    
    // need to deal with 2 cases, 0.000 to 9.999kWh and 10.000 to 99.999kWh
    if (received[1] == '.') {
      // select only the bits of the recevied data we are interested in
      char whtodayArray[5] = {received[0],received[2],received[3],received[4]};
      // convert to a number we can use
      whtoday = atoi(whtodayArray);
    } else if (received[2] == '.') {
      // select only the bits of the recevied data we are interested in
      char whtodayArray[6] = {received[0],received[1],received[3],received[4],received[5]};
      // convert to a number we can use
      whtoday = atoi(whtodayArray);
    }
    trys++;
  }
  return whtoday;
}


// check if inverter relay is on or off which shound tell us if inverter is online
boolean get_status()
{
  int inverter = -1;
  int trys = 0;
  while (inverter == -1 && trys < MAXTRIES)
  { 
    // send command to inverter
    xantrexSerial.println("RELAY?");
    delay(50);
    
    // read response into array of chars
    char received[64];
    int count = 0;
    while (xantrexSerial.available() > 0 )
    {
      received[count++] = xantrexSerial.read();
    }
    
    char statusArray[4] = {received[0],received[1],received[2]};
    //char statusArray[4] = "ERR"; // test string to see how it handles errors
    // compare array to "OFF" if it matches inverter relay is off
    if (strcmp(statusArray, "OFF") == 0) 
    {
      return 0;
    }
    // compary array to "OFF" if the difference is 8 then the word is ON, so the relay is on
    else if (strcmp(statusArray, "OFF") == 8) 
    {
      return 1;
    }
    // if we get down here, the inverter gave us rubbish, repeat till we get useful data
    else
    {
      trys++;
    }
  }
  return -1;
}


// returns ac voltage of inverter
float get_voltage()
{
  // loop till we get a good value
  float voltage = 0;
  int trys = 0;
  while (voltage == 0 && trys < MAXTRIES)
  {
    // send command to inverter
    xantrexSerial.println("VOUT?");
    delay(50);
    
    // read response into array of chars
    char received[64];
    int count = 0;
    while (xantrexSerial.available() > 0 )
    {
      received[count++] = xantrexSerial.read();
    }
  
    // select only the bits of the recevied data we are interested in
    char voltageArray[5] = {received[0],received[1],received[2],received[4]};
    // convert to a number we can use
    voltage = atof(voltageArray)/10;
    trys++;
  }
  return voltage;
}
