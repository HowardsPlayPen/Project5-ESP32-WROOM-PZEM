#include <WiFi.h>
#include <AsyncTCP.h>

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include <Preferences.h>


#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include <SparkLine.h>

AsyncWebServer server(80);

// This is the ESP32 library that gives us access to the onboard EEPROM / Flash
Preferences preferences;

// On the ESP32 there are two HARDWARE SPI ports - one called VSPI (default) and the other HSPI
// On my board, HSPI will be used for the TFT LCD and VSPI is used for RFM68HW 
// The board is using the STANDARD defined pins for HSPI (so in a way the following defines are redundant)
#define HSPI_MISO   12
#define HSPI_MOSI   13
#define HSPI_SCLK   14
#define HSPI_SS     15

// This is a board specific pin number - i.e. for this TFT we need a DC pin and I have designed the PCB with it on GPIO 2
#define TFT_DC 2

// Simple structure to hold key parameters for Core 1
struct core1_state
{  
  //uninitalised pointers to SPI objects
  SPIClass * hspi = NULL;
  Adafruit_ST7789 * tft = NULL; 

};

core1_state tftState;

unsigned long ota_progress_millis = 0;

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}


void displayPage()
{
  tftState.tft->setTextColor(ST77XX_WHITE,ST77XX_BLACK);

  tftState.tft->setCursor(35, 2);
  tftState.tft->setTextSize(3);
  tftState.tft->println(WiFi.localIP());  
}

/// On this board - we have a 240x280 tft led screen hooked up to HSPI
void setup_tft()
{
  //initialise an instance of the SPIClass attached to HSPI respectively
  tftState.hspi = new SPIClass(HSPI);
  
  //initialise hspi with default pins
  //SCLK = 14, MISO = 12, MOSI = 13, SS = 15
  tftState.hspi->begin();

  tftState.tft = new Adafruit_ST7789(tftState.hspi,HSPI_SS, TFT_DC, -1);
  tftState.tft->init(240, 280);           // Init ST7789 280x240
  // default rotation is if the screen was rotated 90 deg clockwise
  // rotation 1 is upside down
  // rotation 2 is rotated 90 anti-clockwise
  tftState.tft->setRotation(3);

  tftState.tft->fillScreen(ST77XX_BLACK);
  Serial.println(F("TFT initialised"));

  displayPage();
}

void setup() {
  Serial.begin(115200);

  preferences.begin("credentials", false); 
  Serial.println("Reading from onboard Flash");

  String ssid = preferences.getString("ssid", ""); 
  String password = preferences.getString("password", "");

  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connected");


  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // We store the username and password for the OTA update side in Flash - i.e. so that when you wish to update the firmware for this board you need to have knowledge of the board specific OTA password
  String elegantUser = preferences.getString("elegantUser", ""); 
  String elegantPassword = preferences.getString("elegantPassword", ""); 

  Serial.println("OTA user: " + elegantUser);

  // Set Authentication Credentials
  ElegantOTA.setAuth(elegantUser.c_str(), elegantPassword.c_str());

  // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  //   request->send(200, "text/plain", "Hi! This is AsyncDemo.");
  // });

  // Note: ElegantOTA listens on the url "/update"
  ElegantOTA.begin(&server);    // Start ElegantOTA

  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
  Serial.println("HTTP server started");

  setup_tft();
}

void loop(void) {
  ElegantOTA.loop();
}