/*
 By Walter Dunckel, K6ATV - radiohound@gmail.com

 Read UBX over I2C using Ublox module CAM-M8Q, SAM-M8Q, NEO-M8P, ZED-F9P, etc
 Code was borrowed from the following sources:
 https://github.com/lightaprs/LightTracker-1.1-433
 https://github.com/sparkfun/SparkFun_u-blox_GNSS_Arduino_Library
 RadioLib examples
 STM32duino examples
 
 This example is made for Ebyte's E77 400 development board, and transmits LoRa APRS at the standerd
 433.775 mhz, which is used in the USA and most other countries. It reads the UBX from the Ublox 
 module over I2c and outputs  some of the info to the serial port. It uses LoRa APRS eMic encoding 
 to transmit location (RadioLib). The E77 module is interesting, because both a STM32WLE mcu and
 a SX1262 transciever are built into one chip, making the module very small and light. However
 the development board is not especially light. It weighs 9.4 grams as assembled. By removing some 
 of its pins and the sma antenna connector, the weight can be reduced to about 6.5 grams. 

 Code currently puts the mcu to sleep between transmissions, but does not yet put the GPS to sleep. 

 Hardware Connections:
 Using Ebyte's E77 400 development board, the I2C connections to the board are PA9 SCL, and PA10 SDA
 (There may also be other pins that will work)
 Open the (usb) serial monitor at 115200 baud to see the output
*/
#include <SPI.h>
#include <SubGhz.h>
#include <Wire.h> //Needed for I2C to GPS
#include <MicroNMEA.h> //http://librarymanager/All#MicroNMEA  - Not really using this anymore should remove
#include "STM32RTC.h"
#include "STM32LowPower.h"
#include <RadioLib.h>


//#include "SparkFun_Ublox_Arduino_Library.h" // this is older library and is no longer supported
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> //http://librarymanager/All#SparkFun_u-blox_GNSS this is the newer library
SFE_UBLOX_GNSS myGPS; // changed from SFE_UBLOX_GPS (the old library)

// Not using MicroNMEA anymore - can remove that 
char nmeaBuffer[100];
char* status = "LoRa TX STM32WLE5";
MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));
bool ublox_high_alt_mode_enabled = false;


// no need to configure pins, signals are routed to the radio internally
STM32WLx radio = new STM32WLx_Module();


APRSClient aprs(&radio);


// set RF switch configuration for EBytes E77 dev board
// PB3 is an LED - activates while transmitting
// NOTE: other boards may be different!
//       Some boards may not have either LP or HP.
//       For those, do not set the LP/HP entry in the table.
static const uint32_t rfswitch_pins[] =
                        {PA6,  PA7,  PB3, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
 {STM32WLx::MODE_IDLE,  {LOW,  LOW,  LOW}},
 {STM32WLx::MODE_RX,    {LOW, HIGH, LOW}},
 {STM32WLx::MODE_TX_LP, {HIGH, LOW, HIGH}},
 {STM32WLx::MODE_TX_HP, {HIGH, LOW, HIGH}},
 END_OF_MODE_TABLE,
};


void setupUBloxDynamicModel() {
   // each time we power the GPS on, we will have to reset this parameter (I don't think the backup battery will hold this setting)
   // If we are going to change the dynamic platform model, let's do it here.
   // Possible values are:
   // PORTABLE, STATIONARY, PEDESTRIAN, AUTOMOTIVE, SEA, AIRBORNE1g, AIRBORNE2g, AIRBORNE4g, WRIST, BIKE
   //DYN_MODEL_AIRBORNE1g (2g and 4g) model increases ublox max. altitude limit from 12.000 meters to 50,000 meters.
 if (myGPS.setDynamicModel(DYN_MODEL_AIRBORNE1g) == false) // Set the dynamic model to DYN_MODEL_AIRBORNE1g 
   {
     Serial.println(F("***!!! Warning: setDynamicModel failed !!!***"));
   }
     else
   {
     ublox_high_alt_mode_enabled = true;
     Serial.print(F("SetDynamicModel: "));
     Serial.println(myGPS.getDynamicModel());
     #if defined(DEVMODE)
       Serial.print(F("Ublox dynamic platform model (DYN_MODEL_AIRBORNE4g) changed successfully! : "));
       Serial.println(myGPS.getDynamicModel());
     #endif 
   }
 }


void setupLoRaRadio()  {
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);
 // set RF switch control configuration
 // this has to be done prior to calling begin()
 //radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);


 // initialize STM32WL with default settings, except frequency
 Serial.print(F("[STM32WL] Initializing ... "));
 //int state = radio.begin(433.775);
 // frequency:                   433.775 MHz
 // bandwidth:                   125 kHz
 // spreading factor:            12
 // coding rate:                 4/5
 int state = radio.begin(433.775, 125, 12, 5);


 if (state == RADIOLIB_ERR_NONE) {
   Serial.println(F("success!"));
 } else {
   Serial.print(F("failed, code "));
   Serial.println(state);
   while (true) { delay(10); }
 }


 // initialize APRS client
 Serial.print(F("[APRS] Initializing ... "));
 // symbol:                      '>' (car)
 // callsign                     "NOCALL"  // your call sign
 // SSID                         1
 char source[] = "K6ATV"; // insert your amateur radio callsign here
 state = aprs.begin('O', source, 7);


 if(state == RADIOLIB_ERR_NONE) {
   Serial.println(F("success!"));
 } else {
   Serial.print(F("failed, code "));
   Serial.println(state);
   while (true) { delay(10); }
 }


 // set appropriate TCXO voltage for Nucleo WL55JC1, WL55JC2, or E77 boards
 state = radio.setTCXO(1.7);
 Serial.print(F("Set TCXO voltage ... "));
  if (state == RADIOLIB_ERR_NONE) {
   Serial.println(F("success!"));
 } else {
   Serial.print(F("failed, code "));
   Serial.println(state);
   while (true) { delay(10); }
 }


 // set output power to 22 dBm (accepted range is -17 - 22 dBm)
 if (radio.setOutputPower(22) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
   Serial.println(F("Selected output power is invalid for this module!"));
   while (true) { delay(10); }
 }


}


void setup()
{
 Serial.begin(115200);
 Serial.println("SparkFun Ublox Example");


 Wire.begin();
 Wire.setClock(400000);


 if (myGPS.begin() == false)
 {
   Serial.println(F("Ublox GPS not detected at default I2C address. Please check wiring. Freezing."));
   while (1);
 }
 setupUBloxDynamicModel();

  // do not overload the buffer system from the GPS, disable UART output
  myGPS.setUART1Output(0); //Disable the UART1 port output
  myGPS.setUART2Output(0); //Disable Set the UART2 port output
  myGPS.setI2COutput(COM_TYPE_UBX); //Set the I2C port to output UBX only (turn off NMEA noise)

  //myGPS.enableDebugging(); //Enable debug messages over Serial (default)

  myGPS.setNavigationFrequency(1);//Set output to 2 times a second. Max is 10
  byte rate = myGPS.getNavigationFrequency(); //Get the update rate of this module
  Serial.print("Current update rate for GPS: ");
  Serial.println(rate);

  myGPS.saveConfiguration(); //Save the current settings to flash and BBR  
 setupLoRaRadio();
 LowPower.begin();
}


void loop()
{
 delay(400); // Wakeup!
 setup(); // go through setup again, becouse I was sleeping, and cant remember anything
 char destination[] = "APZATV"; // APZxxx means experimental. More info can be read on page 13-14 of http://www.aprs.org/doc/APRS101.PDF
 if(!ublox_high_alt_mode_enabled){
   setupUBloxDynamicModel();
 }
 myGPS.checkUblox(); //See if new data is available. Process bytes as they come in.

 if (myGPS.getPVT() && (myGPS.getFixType() !=0) && (myGPS.getSIV() > 3))
 {
   float latitude_mdeg = (myGPS.getLatitude() / 10000000.f);
   Serial.print("Latitude (deg): ");
   Serial.print(latitude_mdeg);


   float longitude_mdeg = (myGPS.getLongitude() / 10000000.f);
   Serial.print(" , Longitude (deg): ");
   Serial.print(longitude_mdeg);

  


   long speed = myGPS.getGroundSpeed();
   Serial.print(F(" Speed: "));
   Serial.print(speed / 514.4);
   Serial.print(F(" (knots)"));


   long heading = myGPS.getHeading();
   Serial.print(F(" Heading: "));
   Serial.print((heading / 1000000));
   Serial.print(F(" degrees"));


   long altitude = myGPS.getAltitude();
   Serial.print(F(" Alt: "));
   Serial.print(altitude/1000);
   Serial.print(F(" (m)"));


   long altitudeMSL = myGPS.getAltitudeMSL();
   Serial.print(F(" AltMSL: "));
   Serial.print(altitudeMSL /1000);
   Serial.print(F(" (m)"));


   byte SIV = myGPS.getSIV();
   Serial.print(F(" SIV: "));
   Serial.print(SIV);


   //Serial.println();
   Serial.print(" Date: ");
   Serial.print(myGPS.getYear());
   Serial.print("-");
   Serial.print(myGPS.getMonth());
   Serial.print("-");
   Serial.print(myGPS.getDay());
   Serial.print(" ");
   Serial.print(myGPS.getHour());
   Serial.print(":");
   Serial.print(myGPS.getMinute());
   Serial.print(":");
   Serial.println(myGPS.getSecond());
   
   int state = aprs.sendMicE((latitude_mdeg), (longitude_mdeg), (heading / 1000000), (speed / 514.4), RADIOLIB_APRS_MIC_E_TYPE_EN_ROUTE, NULL, 0, NULL, status, (myGPS.getAltitudeMSL() / 1000));
   delay(2000);


   if(state == RADIOLIB_ERR_NONE) {
     Serial.println(F("success!"));
   } else {
   Serial.print(F("failed, code "));
   Serial.println(state);
   }
 }
 else
 {
   Serial.print("No Fix - ");
   Serial.print("Num. satellites: ");
   Serial.println(myGPS.getSIV());
 }
 delay(500); // let printing complete prior to sleeping
 LowPower.deepSleep(115000); // with other delays and timing, this tx's approx every 2 min
}

// The below function and other NMEA related code should be removed, as its not being used
// The code now uses UBX 

//This function gets called from the SparkFun Ublox Arduino Library
//As each NMEA character comes in you can specify what to do with it
//Useful for passing to other libraries like tinyGPS, MicroNMEA, or even
//a buffer, radio, etc.
void SFE_UBLOX_GNSS::processNMEA(char incoming)
{
 //Take the incoming char from the Ublox I2C port and pass it on to the MicroNMEA lib
 //for sentence cracking
 nmea.process(incoming);
}


