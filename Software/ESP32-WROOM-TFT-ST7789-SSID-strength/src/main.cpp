#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include <WiFi.h>


// On the ESP32 there are two HARDWARE SPI ports - one called VSPI (default) and the other HSPI
// On my board, HSPI will be used for the TFT LCD and VSPI is used for RFM68HW 
// The board is using the STANDARD defined pins for HSPI (so in a way the following defines are redundant)
#define HSPI_MISO   12
#define HSPI_MOSI   13
#define HSPI_SCLK   14
#define HSPI_SS     15

// This is a board specific pin number - i.e. for this TFT we need a DC pin and I have designed the PCB with it on GPIO 2
#define TFT_DC 2

/// Note: The TFT / SPI api has param for  a reset pin - but for this board I have connected RESET of TFT to the "enable" pin of the ESP32 - i.e. it resets upon startup

///static const int spiClk = 1000000; // 1 MHz

//uninitalised pointers to SPI objects
SPIClass * hspi = NULL;

Adafruit_ST7789 * tft = NULL; 




/// See tftdraw.cpp for the implementation - copied from the Arduino library examples
void testlines(uint16_t color);
void testdrawtext(char *text, uint16_t color);
void testfastlines(uint16_t color1, uint16_t color2);
void testdrawrects(uint16_t color);
void testfillrects(uint16_t color1, uint16_t color2);
void testfillcircles(uint8_t radius, uint16_t color);
void testdrawcircles(uint8_t radius, uint16_t color);
void testtriangles();
void testroundrects();
void tftPrintTest();
void mediabuttons();

void setup() {
  Serial.begin(115200);
  Serial.println(F("ST7789 TFT Test"));

  WiFi.begin();

  //initialise an instance of the SPIClass attached to HSPI respectively
  hspi = new SPIClass(HSPI);
  
  //initialise hspi with default pins
  //SCLK = 14, MISO = 12, MOSI = 13, SS = 15
  hspi->begin();

  tft = new Adafruit_ST7789(hspi,HSPI_SS, TFT_DC, -1);
  tft->init(240, 280);           // Init ST7789 280x240
  // default rotation is if the screen was rotated 90 deg clockwise
  // rotation 1 is upside down
  // rotation 2 is rotated 90 anti-clockwise
  tft->setRotation(3);

  tft->fillScreen(ST77XX_BLACK);
  Serial.println(F("TFT initialised"));

  tft->setTextSize(2); // Large text  
  tft->setTextColor(ST77XX_WHITE);
}

const int scanTime = 600; // Scan time in milliseconds

void loop()
{
  int n = WiFi.scanNetworks();  

  tft->fillScreen(ST77XX_BLACK);
  tft->setCursor(0, 20);

  if (n == 0) {  
    tft->println("no networks found");
  } else {
    for(int i = 0; i < n; ++i)
    {
      tft->print(WiFi.SSID(i));
      tft->print(" (");
      tft->print(WiFi.RSSI(i));
      tft->print(") ");
      tft->println(WiFi.channel(i));
    }
  }  
  delay(scanTime);
}

