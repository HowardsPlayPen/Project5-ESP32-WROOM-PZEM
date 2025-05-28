// Sample RFM69 sender/node sketch for the MotionMote
// https://lowpowerlab.com/guide/motionmote/
// PIR motion sensor connected to D3 (INT1)
// When RISE happens on D3, the sketch transmits a "MOTION" msg to receiver Moteino and goes back to sleep
// In sleep mode, Moteino + PIR motion sensor use about ~60uA
// With Panasonic PIRs it's possible to use only around ~9uA, see guide link above for details
// IMPORTANT: adjust the settings in the configuration section below !!!
// **********************************************************************************
// Copyright Felix Rusu of LowPowerLab.com, 2018
// RFM69 library and sample code by Felix Rusu - lowpowerlab.com/contact
// **********************************************************************************
// License
// **********************************************************************************
// This program is free software; you can redistribute it 
// and/or modify it under the terms of the GNU General    
// Public License as published by the Free Software       
// Foundation; either version 3 of the License, or        
// (at your option) any later version.                    
//                                                        
// This program is distributed in the hope that it will   
// be useful, but WITHOUT ANY WARRANTY; without even the  
// implied warranty of MERCHANTABILITY or FITNESS FOR A   
// PARTICULAR PURPOSE. See the GNU General Public        
// License for more details.                              
//
// Licence can be viewed at                               
// http://www.gnu.org/licenses/gpl-3.0.txt
//
// Please maintain this license information along with authorship
// and copyright notices in any redistribution of this code
// **********************************************************************************
#include <RFM69.h>         //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>     //get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>           //comes with Arduino IDE (www.arduino.cc)
//#include <SPIFlash.h>      //get it here: https://www.github.com/lowpowerlab/spiflash
#include <Wire.h>          //comes with Arduino

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

//****************************************************************************************************************
//**** IMPORTANT RADIO SETTINGS - YOU MUST CHANGE/CONFIGURE TO MATCH YOUR HARDWARE TRANSCEIVER CONFIGURATION! ****
//****************************************************************************************************************
#define GATEWAYID     1     //ID of  your main/gateway/receiver node (can be any ID but good to keep this as 1 or an easy to remember number)
#define NODEID        2  //88    //unique for each node on same network
#define NETWORKID     100  //the same on all nodes that talk to each other
//Match frequency to the hardware version of the radio on your Moteino (uncomment one):
#define FREQUENCY     RF69_433MHZ
//#define FREQUENCY     RF69_868MHZ
//#define FREQUENCY     RF69_915MHZ
#define IS_RFM69HW_HCW  //uncomment only for RFM69HW/HCW! Leave out if you have RFM69W/CW!
#define ENCRYPTKEY    "sampleEncryptKey" //exactly the same 16 characters/bytes on all nodes!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
#define ATC_RSSI      -75
//#define ENABLE_BME280 //uncomment to allow reading the BME280 (if present)
//*********************************************************************************************
#define ACK_TIME      30  // max # of ms to wait for an ack
#define ONBOARDLED     9  // Moteinos have LEDs on D9
//#define LED            5  // MotionOLEDMote has an external LED on D5
//#define MOTION_PIN     3  // D3
//#define MOTION_IRQ     1  // hardware interrupt 1 (D3) - where motion sensors OUTput is connected, this will generate an interrupt every time there is MOTION
//#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
//#define BATT_FORMULA(reading) reading * 0.00322 * 1.49 // >>> fine tune this parameter to match your voltage when fully charged
                                                       // details on how this works: https://lowpowerlab.com/forum/index.php/topic,1206.0.html
#define DUPLICATE_INTERVAL 20000 //avoid duplicates in 55second intervals (ie mailman sometimes spends 30+ seconds at mailbox)
#define BATT_INTERVAL  300000  // read and report battery voltage every this many ms (approx)

#define SERIAL_EN             //comment this out when deploying to an installed Mote to save a few KB of sketch size
#define SERIAL_BAUD    115200
#ifdef SERIAL_EN
#define DEBUG(input)   {Serial.print(input); }
#define DEBUGln(input) {Serial.println(input); }
#define DEBUGFlush() { Serial.flush(); }
#else
#define DEBUG(input);
#define DEBUGln(input);
#define DEBUGFlush();
#endif


//#define FLASH_SS      8 // and FLASH SS on D8 on regular Moteinos (D23 on MoteinoMEGA)
//SPIFlash flash(FLASH_SS, 0xEF30); //EF30 for 4mbit  Windbond chip (W25X40CL)


volatile boolean motionDetected=false;
float batteryVolts = 5;
char BATstr[10]; //longest battery voltage reading message = 9chars
char sendBuf[32];
byte sendLen;



///////// Parameters needed for the TFT screen - to display debug diagnostics

// On the ESP32 there are two HARDWARE SPI ports - one called VSPI (default) and the other HSPI
// On my board, HSPI will be used for the TFT LCD and VSPI is used for RFM68HW 
// The board is using the STANDARD defined pins for HSPI (so in a way the following defines are redundant)
#define HSPI_MISO   12
#define HSPI_MOSI   13
#define HSPI_SCLK   14
#define HSPI_SS     15

// This is a board specific pin number - i.e. for this TFT we need a DC pin and I have designed the PCB with it on GPIO 2
#define TFT_DC 2

#define RFM69_CS  5
#define RFM69_DIO  4

#define RF69_IRQ_PIN RFM69_DIO

/// Note: The TFT / SPI api has param for  a reset pin - but for this board I have connected RESET of TFT to the "enable" pin of the ESP32 - i.e. it resets upon startup

//uninitalised pointers to SPI objects
SPIClass * vspi = NULL;
SPIClass * hspi = NULL;
Adafruit_ST7789 * tft = NULL; 

#ifdef ENABLE_ATC
  RFM69_ATC* radio;
#else
  RFM69* radio;
#endif

/**
 * INITIALISATION 
 * 
 */
void setup() {
  Serial.begin(SERIAL_BAUD);

  /////// TFT SCREEN initialisation

  //initialise an instance of the SPIClass attached to HSPI respectively
  hspi = new SPIClass(HSPI);
  vspi = new SPIClass(VSPI); //SPI bus normally attached to pins 5, 18, 19 and 23,
  
  //initialise hspi with default pins
  //SCLK = 14, MISO = 12, MOSI = 13, SS = 15
  hspi->begin();

  //SCLK = 18, MISO = 19, MOSI = 23, SS = 5
  vspi->begin();

  tft = new Adafruit_ST7789(hspi,HSPI_SS, TFT_DC, -1);
  tft->init(240, 280);           // Init ST7789 280x240
  // default rotation is if the screen was rotated 90 deg clockwise
  // rotation 1 is upside down
  // rotation 2 is rotated 90 anti-clockwise
  tft->setRotation(3);

  tft->fillScreen(ST77XX_RED);//
  Serial.println(F("TFT initialised"));

  tft->setCursor(0, 50);
  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(2);
  tft->println("Screen initialised ");


  radio = new RFM69_ATC(RFM69_CS,RF69_IRQ_PIN,true, vspi);

  bool ret = radio->initialize(FREQUENCY,NODEID,NETWORKID);
  if(ret)
  {
    tft->println("Radio initialised OK");
    radio->readAllRegs();
  }
  else
    tft->println("Radio initialised FAILED");

#ifdef IS_RFM69HW_HCW
  radio->setHighPower(); //must include this only for RFM69HW/HCW!
#endif
  tft->println("Set high power ");
  radio->encrypt(ENCRYPTKEY);
  tft->println("Encrypt on ");


//Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
//For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
//For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
//Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
#ifdef ENABLE_ATC
  radio->enableAutoPower(ATC_RSSI);
#endif
  tft->println("Enable ATC");
  
  radio->spyMode(true); // Sniff out all network radio traffic

  //pinMode(MOTION_PIN, INPUT);
  //attachInterrupt(MOTION_IRQ, motionIRQ, RISING);
  char buff[50];
  sprintf(buff, "\nTransmitting at %d Mhz...", FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  DEBUGln(buff);

  radio->send(2, "START", 5,false);
#ifdef SENDRETRY
  bool r = radio->sendWithRetry(2, "START", 5,0,0);
  tft->print("Send with retry ");
  if(r)
  {
    tft->print(" OK");
  }
  else{
    tft->print(" FAIL");
  }
#endif  
  
#ifdef ENABLE_ATC
  DEBUGln("RFM69_ATC Enabled (Auto Transmission Control)\n");
#endif
  
  tft->println("Finished setup");
  delay(2000);
  tft->fillScreen(ST77XX_BLACK);
}

byte ackCount=0;
uint32_t packetCount = 0;
uint32_t loopCounter=0;

 uint32_t sending=0;

void display()
{
  //tft->fillScreen(ST77XX_BLACK);
  tft->setCursor(0, 50);
  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(2);
  tft->print("RSSI ");
  tft->println(radio->RSSI);

  tft->print("Ack ");
  tft->println(ackCount);

  tft->print("Packet ");
  tft->println(packetCount);

  tft->print("SEND ");
  tft->println(sending);
}

/*
uint16_t batteryReportCycles=0;
uint32_t curr_time=0, now=0, MLO=0, BLO=0;
byte motionRecentlyCycles=0;
*/


uint32_t now=0;



void loop() {
  now = millis();
  
  if (radio->receiveDone())
  {
    Serial.print("#[");
    Serial.print(++packetCount);
    Serial.print(']');
    Serial.print('[');Serial.print(radio->SENDERID, DEC);Serial.print("] ");
    if (true)
    {
       Serial.print("to [");
       Serial.print(radio->TARGETID, DEC);
       Serial.print("] ");
    }
    for (byte i = 0; i < radio->DATALEN; i++)
      Serial.print((char)radio->DATA[i]);
    Serial.print("   [RX_RSSI:");Serial.print(radio->RSSI);Serial.print("]");
    
    if (radio->ACKRequested())
    {
      byte theNodeID = radio->SENDERID;
      radio->sendACK();
      Serial.print(" - ACK sent.");

      // When a node requests an ACK, respond to the ACK
      // and also send a packet requesting an ACK (every 3rd one only)
      // This way both TX/RX NODE functions are tested on 1 end at the GATEWAY
      if (ackCount++%3==0)
      {
        Serial.print(" Pinging node ");
        Serial.print(theNodeID);
        Serial.print(" - ACK...");
        delay(3); //need this when sending right after reception .. ?

        radio->send(theNodeID, "ACK TEST", 8,false);

#ifdef SENDRETY        
        if (radio->sendWithRetry(theNodeID, "ACK TEST", 8, 0))  // 0 = only 1 attempt, no retries
          Serial.print("ok!");
        else Serial.print("nothing");
#endif        
      }
    }    
  }

  if(loopCounter>10)
  {
    loopCounter=0;
    ++sending ;
    radio->send(GATEWAYID, "ACK TEST", 8,false);
  }
  else{
    ++loopCounter;
  }


  display();
  delay(100);  
}
