/// Project to use the newest PCB with a ESP32-WROOM and a TFT 1.69' screen 240z280 along with a PZEM-004T in order
/// to monitor the power / electric usage in our household and transmit the information over MQTT
/// It will show information on the TFT screen - such as line charts of power usage etc

/// The aim is to use both CPU cores - one for the PZEM monitoring and display and the other for monitoring 
/// the network side / the MQTT connection and capacitiative touch buttons


#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <Arduino.h>
#include <WiFi.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include <SparkLine.h>

/// Used for the simple MQTT publisher
#include <PubSubClient.h>

#include "ssdp_helper.hpp"

#include <PsychicHttp.h>
#include <ElegantOTA.h>
#include <WiFi.h>

#include "json_settings.hpp"
#include "ota_default.hpp"

/// Below are the PZEM specific bits - i.e. initialising the right GPIO to use etc
#include <HardwareSerial.h>

// The below "define" controls whether it uses the V3 or V2 serial interface (which depends on which PZEM-004T board you have).
// V3 outputs a few extra parameters and the board has leds that pulse when readings are being taken
#define PZEM_V3


/// NOTE 10.0.1.75 - solar monitor
/// NOTE 10.0.1.7 - house 240V monitor

//#define SOLAR

#ifdef PZEM_V3
/// The below include is when we have a NEW version of PZEM (v3)
#include <PZEM004Tv30.h> // See https://github.com/mandulaj/PZEM-004T-v30

#else
// This is for the OLDER version of PZEM (v2.0)
#include <PZEM004T.h>   // See https://github.com/olehs/PZEM004T
#endif

#include <memory>
#include <vector>


//Hardware Serial on two GPIO
/// CONNECT (ESP32 RX) to TX on PZEM
/// CONNECT (ESP32 TX) to RX on PZEM
#define RX2 16
#define TX2 17



// On the ESP32 there are two HARDWARE SPI ports - one called VSPI (default) and the other HSPI
// On my board, HSPI will be used for the TFT LCD and VSPI is used for RFM68HW 
// The board is using the STANDARD defined pins for HSPI (so in a way the following defines are redundant)
#define HSPI_MISO   12
#define HSPI_MOSI   13
#define HSPI_SCLK   14
#define HSPI_SS     15

// This is a board specific pin number - i.e. for this TFT we need a DC pin and I have designed the PCB with it on GPIO 2
#define TFT_DC 2

HardwareSerial SerialPZEM( 2 ); // ESP32 has 3 hardware serials - 0 is used for FTDI / UART, we will use the 3rd one


#ifdef PZEM_V3

PZEM004Tv30 pzem(SerialPZEM,RX2,TX2);

#else
PZEM004T pzem(&Serial2,RX2,TX2);

IPAddress ip(192,168,1,1); // For some reason the older version of the PZEM library wants to be given an IP address so we assign a random one...

#endif

// Define the REST server instance
PsychicWebSocketHandler websocketHandler;
PsychicEventSource eventSource;
PsychicHttpServer server;


// Rest endpoint to allow resetting of the accumulated power meter in the PZEM 
esp_err_t reset_pzem(PsychicRequest *request)
{
  PsychicResponse response(request);
  response.setCode(200);
  response.setContentType("text/json");

  bool ret = pzem.resetEnergy();
  
  const char * txt = ret ? "{\"reset\":\"true\"}" : "{\"reset\":\"false\"}";
  response.setContent((const uint8_t*)txt,strlen(txt));
  return response.send();
}


// Enum to keep track of the current state for each button
enum TouchState {TouchUndefined, ButtonUp, ButtonFirstPress, ButtonPressed, ButtonDown};

/// Note: The TFT / SPI api has param for  a reset pin - but for this board I have connected RESET of TFT to the "enable" pin of the ESP32 - i.e. it resets upon startup
struct core1_state
{  
  //uninitalised pointers to SPI objects
  SPIClass * hspi = NULL;
  Adafruit_ST7789 * tft = NULL; 

  bool isDirty = true; // Does the screen need a refresh

  uint8_t pzemAddress = 0;
  float voltage=0.0;
  float current=0.0;
  float power=0.0;
  float energy=0.0;
  float frequency=0.0;
  float pf=0.0;

  int pageNumber = 0; // We have multiple pages for display in this app - changed by pressing the capacitative 'touch' button

  bool pzemConnected = false;

  /// Sparklines let us draw simple 2d line charts that update as new values are pushed in - this will show the power usage
  std::shared_ptr<SparkLine<uint16_t> > powerUsage; // Keep track of the latest X values (where X is set in the constructor below)

};

struct core2_state
{
  bool networkConnected = false;
  bool mqttConnected = false;

  /// MQTT details
  String mqtt_server = "";

  String mqtt_user = "iot"; // Again, this is hardwired to an MQTT user on Pidesktop1
  String mqtt_password = "iot1";

  String mqtt_topicOUT ="/esp32/Electricity/";

  /// TEMPORARY HACK - FOR NOW I KNOW THAT THE PZEM IN THE HOUSE IS V3 and is for the household power and the
  /// PZEM in the workshop is V2 and for monitoring the solar - hence this temporary hardwiring...
#ifndef SOLAR
  String mqtt_topicName = "house";   // "house" for the household power usage and "solar" for the Solar PV generated power
#else
  String mqtt_topicName = "solar";   // "house" for the household power usage and "solar" for the Solar PV generated power
#endif
  String mqtt_port  = "1883";  

  char mqtt_client_id[23]; // This is auto generated in connection functions below 
  std::vector< std::pair< String, int32_t> > ssidList;

  String ssdp_name;
  String ssdp_modelname;
};


bool setup_mqtt();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT(int retryCount);

const int numberTouchPins = 3; /// Ensure this value is consistent with the C array below
const int touchPins[]={T9,T8,T7}; 
bool touchPinsState[] = {false,false,false}; // Keep track of the read in value

TouchState touchPinsInternalState[] = {TouchUndefined,TouchUndefined,TouchUndefined};

const uint16_t touchPressThreshold = 20; // Magic number:  This is determined / calibrated by doing a simple runtime test of the board and seeing what is returned from touchRead()

/// Main state variables
core1_state tftState;
core2_state networkState;
TaskHandle_t Task1;  // Second thread for managing network / mqtt and touch buttons

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Simple handler for index page 
esp_err_t get_index_html(PsychicRequest *request)
{
  PsychicResponse response(request);
  response.setCode(200);
  response.setContentType("text/json");

  String json = "{\"v\":" + String(tftState.voltage) + "}";   
  response.setContent((const uint8_t*)json.c_str(),json.length());
  return response.send();
}


void NetworkThreadCode( void * parameter); // Fwd declare function for the second thread
void extractPZEM_Info();

void displayPage0(bool fullRedraw)
{
  if(fullRedraw)
  {
    tftState.tft->fillScreen(ST77XX_BLACK);
  }
  tftState.tft->setTextColor(ST77XX_WHITE,ST77XX_BLACK);

  tftState.tft->setCursor(35, 2);
  tftState.tft->setTextSize(2);
  tftState.tft->println("0");


  tftState.tft->setCursor(65, 2);
  tftState.tft->setTextSize(2);
  tftState.tft->println("ETH");
  uint16_t col = networkState.networkConnected ? ST77XX_GREEN : ST77XX_RED;
  tftState.tft->fillRect(105,2,20,20,col);

  tftState.tft->setCursor(135, 2);
  tftState.tft->setTextSize(2);
  tftState.tft->println("MQTT");
  col = networkState.mqttConnected ? ST77XX_GREEN : ST77XX_RED;
  tftState.tft->fillRect(185,2,20,20,col);  

/// PZEM address looks pretty bland so removing it from the display (for now?)

  tftState.tft->setTextSize(2); 
  tftState.tft->setCursor(0,25); 
  tftState.tft->print(String(tftState.pzemAddress));


  tftState.tft->setTextSize(3);
  tftState.tft->setCursor(0,50); 
  tftState.tft->print(String(tftState.voltage) + " V");

  tftState.tft->setCursor(0,90); 
  tftState.tft->print(String(tftState.current) + " A");

  tftState.tft->setCursor(0,130); 
  tftState.tft->print(String(tftState.power) + " W");

  tftState.tft->setCursor(120,130); 
  tftState.tft->print(String(tftState.energy) + " VA");

#ifdef PZEM_V3
  tftState.tft->setCursor(0,160); 
  tftState.tft->print(String(tftState.frequency) + " Hz");

  tftState.tft->setCursor(0,190); 
  tftState.tft->print(String(tftState.pf) + " PF");
#endif  

  //if(!tftState.pzemConnected)
  {
    tftState.tft->setCursor(210, 2);
    tftState.tft->setTextSize(2);
#ifdef PZEM_V3    
    tftState.tft->println("P3");  
#else
    tftState.tft->println("P2");  
#endif
    col = tftState.pzemConnected ? ST77XX_GREEN : ST77XX_RED;
    tftState.tft->fillRect(235,2,20,20,col);  
  }

  
  tftState.tft->setCursor(160, 220);
  tftState.tft->setTextSize(2);
  String ip = WiFi.localIP().toString();
  tftState.tft->println(ip);
}

void displayPage1(bool fullRedraw)
{
  // Always do a full refresh as we are showing a line graph that will update frequently
  //if(fullRedraw)
  {
    tftState.tft->fillScreen(ST77XX_ORANGE);
  }

  tftState.tft->setTextColor(ST77XX_WHITE, ST77XX_ORANGE);

  tftState.tft->setCursor(35, 2);
  tftState.tft->setTextSize(2);
  tftState.tft->println("1");

  // Display the sparkline 2d line graph showing power usage information
  if(tftState.powerUsage)
  {              
      tftState.tft->setTextSize(3);
      tftState.powerUsage->draw(5, 230, 260, 220);
      tftState.tft->setCursor(2,22);
      tftState.tft->print(String(tftState.power));
  }

}

void displayPage2(bool fullRedraw)
{
  if(fullRedraw)
  {
    tftState.tft->fillScreen(ST77XX_ORANGE);
  }

  tftState.tft->setTextColor(ST77XX_WHITE, ST77XX_ORANGE);

  tftState.tft->setCursor(35, 2);
  tftState.tft->setTextSize(2);
  tftState.tft->println("2");

  int y = 50;

  for(int ctr=0; ctr< networkState.ssidList.size(); ++ctr)
  {
    const std::pair<String, int32_t> & item(networkState.ssidList[ctr]);

    tftState.tft->setCursor(10, y);
    tftState.tft->print( item.first);

    tftState.tft->setCursor(220, y);
    tftState.tft->print( item.second);
    
    y+= 20;
  }


}

unsigned long lastDisplay = 0;

// Display loop for the TFT screen - there are multiple pages of info that can be shown
void display(unsigned long now, bool fullRedraw)
{
  if(!tftState.isDirty)
  {
    return;
  }

  if((now - lastDisplay) < 200)
  {
    return;
  }

  lastDisplay = now; // Only refresh every 100ms max

  tftState.isDirty = false;

  switch(tftState.pageNumber)
  {
    case 0:
    displayPage0(fullRedraw);
    break;

    case 1:
    displayPage1(fullRedraw);
    break;

    case 2:
    default:
    displayPage2(fullRedraw);
    break;
  }
}


bool fullRedraw = false;

void setup()
{
  Serial.begin(115200);

  // pinMode(TX2, OUTPUT);  // Set GPIO as output
  // pinMode(RX2, INPUT);   // Set GPIO as input

  Serial.print(F("ESP32-WROOM-PZEM Startup on Core "));
  Serial.println(xPortGetCoreID());

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

  tftState.tft->fillScreen(ST77XX_RED);
  Serial.println(F("TFT initialised - reading "));

  JsonDocument doc;
  if(!LoadJsonSettings(doc))
  {
      /// TODO HANDLE THIS ERROR - e.g. FLASH THE LED
      Serial.println("ERROR: Investigate what went wrong in loading JSON file");
  }

  JsonObject root = doc.as<JsonObject>();

  if(!LoadDefaultParameters(root))
  {
    //// TODO HANDLE THIS ERROR
    Serial.println("ERROR: Investigate what went wrong in processing the JSON object");
  }

  try
  {
    networkState.ssdp_name      =   root["ssdp_name"].as<String>();
    networkState.ssdp_modelname =   root["ssdp_modelname"].as<String>();

    Serial.printf("SSDP Name: (%s), model name (%s)",networkState.ssdp_name.c_str(), networkState.ssdp_modelname.c_str());
    Serial.println("");

    String tmp = root["mqtt_server"].as<String>();

    if(tmp.length())
      networkState.mqtt_server  = tmp;
    
    tmp = root["mqtt_user"].as<String>();

    if(tmp.length())
      networkState.mqtt_user     = tmp;

    tmp = root["mqtt_password"].as<String>();
    
    if(tmp.length())
      networkState.mqtt_password = tmp;

    Serial.printf("MQTT Server: (%s), User (%s), Password (%s)",networkState.mqtt_server.c_str(), networkState.mqtt_user.c_str(), networkState.mqtt_password.c_str());
    Serial.println("");
  }
  catch(const std::exception& e)
  {
    Serial.println(e.what());
  }
  
  Serial.println(F("Starting Network thread"));
  xTaskCreatePinnedToCore(
      NetworkThreadCode, /* Function to implement the task */
      "Network", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &Task1,  /* Task handle. */
      0); /* Core where the task should run */

  Serial.println("CORE1: Setup PZEM");
  // Quite possible that the PZEM instance has already initialised the hardware serial at this point.
  SerialPZEM.begin(9600,SERIAL_8N1,RX2,TX2);  

#ifdef PZEM_V3

#else
  pzem.setAddress(ip);
#endif

  Serial.println("CORE1: Initialised HW Serial");

  Serial.println(pzem.readAddress(), HEX);

  delay(100);

  // Serial.println(F("CORE1: Reset energy"));
  // pzem.resetEnergy();

  Serial.println(F("CORE1: End setup"));
  fullRedraw=true;
}


unsigned long lastPZEMSample=0; // milli second at which we took a PZEM reading

// Main loop for one thread (running on Core 1)
void loop() { 
  
  unsigned long now = millis();

  // Scan through Touch pins
  for(int counter=0; counter<numberTouchPins; ++counter)
  {
    uint16_t touch_sensor = touchRead(touchPins[counter]);     

    if(touch_sensor < touchPressThreshold)
    {
      switch(touchPinsInternalState[counter])
      {
        case TouchUndefined:
        case ButtonUp:
        default:
          touchPinsInternalState[counter] = ButtonFirstPress; // Button must be pressed for two cycles to become 'ButtonDown' - to stop fake presses          
          break;
        case ButtonFirstPress:
          touchPinsInternalState[counter] = ButtonPressed;
          touchPinsState[counter] = true;
          break;
        case ButtonPressed:
          // We differentiate between when the button is first pressed and then separately being held down
          touchPinsInternalState[counter] = ButtonDown;
          break;

        case ButtonDown:
          // Do nothing as button is still being pressed
          break;
      }
    }
    else
    {
      touchPinsInternalState[counter] = ButtonUp;
    }
  }

  if(touchPinsState[0])
  {    
    touchPinsState[0] = false; // Reset the state 
    ++tftState.pageNumber;
    tftState.isDirty = true;
    fullRedraw = true;

    if(tftState.pageNumber >2)
    {
      tftState.pageNumber = 0;
    }
  }

  if(!SerialPZEM)
  {
     Serial.println("HWSerialPZEM not initialised");
  }

  if(SerialPZEM && pzem.isConnected() ) //!tftState.pzemConnected)
  {
    tftState.pzemConnected = true;      
  }

  // Only sample the power information every second max
  if(tftState.pzemConnected && ((now - lastPZEMSample) > 1000))
  {
    lastPZEMSample = now;
    extractPZEM_Info();
  }

  display(now, fullRedraw); 
  fullRedraw = false;

  delay(50);
}

void scanNetworks() {
 
  // TODO - this makes a race condition between this thread scanning the SSIDs and the display function
  networkState.ssidList.clear();

  int numberOfNetworks = WiFi.scanNetworks();
 
  Serial.print("Number of networks found: ");
  Serial.println(numberOfNetworks);
 
  for (int i = 0; i < numberOfNetworks; i++) {
 
    Serial.print("Network name: ");
    Serial.println(WiFi.SSID(i));
 
    Serial.print("Signal strength: ");
    Serial.println(WiFi.RSSI(i));  

    networkState.ssidList.push_back( std::make_pair(WiFi.SSID(i),WiFi.RSSI(i)));
  }
}

void setup_network()
{
  scanNetworks();
  tftState.isDirty = true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password); // These were loaded from the JSON settings file.
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

  networkState.networkConnected = WiFi.status() == WL_CONNECTED;
  tftState.isDirty = true;   
}


// Main loop for second thread (running on Core 0) - this function should not exit as that would cause the ESP32 to abort and reboot(!)
void NetworkThreadCode( void * parameter)
{
  Serial.println(F("Second thread initialised + running on Core "));
  Serial.println(xPortGetCoreID());

  // Startup initialisation code first
  setup_network();

  Serial.print("WIFI configured ");
  Serial.println(WiFi.status() == WL_CONNECTED);

  setup_mqtt(); // TODO should check if network is ok etc
  networkState.mqttConnected = mqttClient.connected();
  tftState.isDirty = true; 

    // Set Authentication Credentials
   ElegantOTA.setAuth(ota_user.c_str(), ota_password.c_str());
  // start server
  server.listen(80); // MUST call listen() before registering any urls using .on()

  ssdp_helper params{"/update",networkState.ssdp_name,"Esp32",networkState.ssdp_modelname,"https://aitchpea.com"};
  
  setup_ssdp(params,server);  // REMEMBER must call thios AFTER server.lkisten()

  server.on("/", HTTP_GET, get_index_html);

  server.on("/reset", HTTP_GET, reset_pzem);

  // The below function registers a handler with the Web server to generically handle HTTP_OPTIONS and add the flags that we are not worried about CORS
  disable_cors(server); // CORS is pointless for an IOT device here

  // Note: ElegantOTA listens on the url "/update"
  ElegantOTA.begin(&server);    // Start ElegantOTA

  //ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  // This is the mainloop for the second thread (and will never exit)
  while(true)
  {
    networkState.networkConnected = WiFi.status() == WL_CONNECTED;
    
    if ( WiFi.status() ==  WL_CONNECTED ) 
    {
      // Allow the MQTT client to do 'keep alives' etc      
      bool ret =  mqttClient.loop(); // returns false if MQTT not connected
      yield(); 

      if(!ret)
      {
        reconnectMQTT(1);
        networkState.mqttConnected = false;
        tftState.isDirty = true; 
      }

    }
    else
    {
      // wifi down, reconnect here
      WiFi.begin();    
      int WLcount = 0;
      while (WiFi.status() != WL_CONNECTED && WLcount < 200 ) 
      {
        delay( 100 );
        ++WLcount;
      }      
    }  

    delay(50);
  }
}


/// Called when new message received over MQTT
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// Attempt to reconnect to MQTT - this will block whilst trying "retryCount" times and then return
void reconnectMQTT(int retryCount=5) {
  // Loop until we're reconnected
  while (!mqttClient.connected() && retryCount>0)
  {
    Serial.println("MQTT re-connecting");
    // Attempt to connect
    if (mqttClient.connect(networkState.mqtt_client_id, networkState.mqtt_user.c_str(), networkState.mqtt_password.c_str())) {
      Serial.println("connected");
    } 
    else
    {
      Serial.println("MQTT fail: " + String(mqttClient.state()));
      // Wait before retrying
      delay(800);
    }
    --retryCount;
  }
}



// If MQTT server parameters have been defined then we try and connect
bool setup_mqtt()
{
  if(networkState.mqtt_server[0] == '0' && networkState.mqtt_server[1] == '.')
  {
    Serial.println("MQTT fail, no server is defined");
    
    return false;
  }

  if(networkState.mqtt_user[0] == '\0')
  {
    Serial.println("MQTT fail, no user is defined");
    return false;
  }

  Serial.println("MQTT -> user: " + String(networkState.mqtt_user) + " Server: " + networkState.mqtt_server.c_str());
  mqttClient.setServer(networkState.mqtt_server.c_str(), 1883);
  mqttClient.setCallback(mqtt_callback);

  uint64_t chipid = ESP.getEfuseMac();

  snprintf(networkState.mqtt_client_id, 23, "ESP32-%08X", (uint32_t)chipid);
  Serial.println(String("MQTT client id: ") + networkState.mqtt_client_id);

  // Ensure we use a random "client id" - Mosquitto MQTT server will disconnect if two clients connect with same id
  
  if(!mqttClient.connect(networkState.mqtt_client_id, networkState.mqtt_user.c_str(), networkState.mqtt_password.c_str()))
  {
    Serial.println("MQTT fail");
    return false;
  }
  
  Serial.println("MQTT ok");
  return true;
}


void extractPZEM_Info()
{
#ifdef PZEM_V3

  Serial.println(String(" IS CONN") + String(pzem.isConnected()));
  tftState.pzemAddress = pzem.getAddress();

  // Read the data from the sensor
  tftState.voltage    = pzem.voltage();
  tftState.current    = pzem.current();
  tftState.power      = pzem.power();
  tftState.energy     = pzem.energy();
  tftState.frequency  = pzem.frequency();
  tftState.pf         = pzem.pf();

#else
  // Read the data from the sensor
  tftState.voltage    = pzem.voltage(ip);
  tftState.current    = pzem.current(ip);
  tftState.power      = pzem.power(ip);
  tftState.energy     = pzem.energy(ip);
#endif

  if(tftState.voltage> 0) // Dont bother sending any MQTT msgs if no readings are present
  {
    tftState.isDirty = true;

    if(!tftState.powerUsage)
      tftState.powerUsage.reset(new SparkLine<uint16_t>(240, [&](const uint16_t x0, const uint16_t y0, const uint16_t x1, const uint16_t y1) { tftState.tft->drawLine(x0, y0, x1, y1, 1);}));

    tftState.powerUsage->add(tftState.power);
    
    String jsonStr = "{\"voltage\": ";
    jsonStr += String(tftState.voltage);
    jsonStr += ", \"current\":";
    jsonStr += String(tftState.current);
    jsonStr += ", \"power\": ";
    jsonStr += String(tftState.power);
    jsonStr += ", \"energy\": ";
    jsonStr += String(tftState.energy);
    jsonStr += ", \"freq\": ";
    jsonStr += String(tftState.frequency);
    jsonStr += ", \"pf\": ";
    jsonStr += String(tftState.pf);
    jsonStr += "}";

    // Handle wifi reconnect / mqtt reconnect etc
    if(mqttClient.connected())
    {
      /// TODO make use of the return code to display an error
      //bool ret = 
      String sensorTopic = networkState.mqtt_topicOUT;
      sensorTopic += networkState.mqtt_topicName;  

      mqttClient.publish(sensorTopic.c_str(),jsonStr.c_str());

      /// TODO display an X on the screen or flash the led or something to show an error
      networkState.mqttConnected = mqttClient.connected();
    }
    /// THIS IS BEING CALLED ON THE DISPLAY THREAD SO WE DO NOT WANT TO BLOCK - THIS SHOULD BE CALLED ON THE NETWORK THREAD
    /*
    else{          
      reconnectMQTT();
      networkState.mqttConnected = false;
      tftState.isDirty = true; 
    }
    */
  }
}