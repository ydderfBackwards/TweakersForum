#include <WiFi.h>
#include <ArduinoOTA.h> //On the air update
#include <HTTPClient.h> //POST to PHP file on server

//Includes for time synchronisation
#include <WiFiUdp.h>
#include <NTPClient.h>

//Includes for display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


//Print debug info or not...
#define printWifiMonitoring 1
#define printSerial2  1
#define printSerialP1Raw 0
#define printSerialP1decoded 1
#define printHTTPClient 1
#define printElectrActualQueue 0





//Display OLED with SSD1306 chip
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


//Wifi
const char* ssid = "FreeWifi";
const char* password = "Welkom01";
IPAddress local_IP(192, 168, 1, 10);
IPAddress gateway(192, 168, 1, 254);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   
IPAddress secondaryDNS(8, 8, 4, 4); 

//Wifi monitoring
long wifiErrorCount = 0; //Start with no errors
long wifiRssiMax = -9999; //Start with a very low max, so the first reading will always be more.
long wifiRssiMin = 9999; //Start with a very high min, so the first reading will always be less.

//Telnet
WiFiServer TelnetServer(23); //Telnet on default port 23
WiFiClient TelnetClient;



//Debug
Print* logger; //Logger will be used for printing to Serial or Telnet monitor.

//GPIO 
const int ledPin = 4;

//Testing
unsigned long previousMillis;        // will store last time LED was updated
const unsigned long interval = 10000;           // interval at which to blink (milliseconds)

//Actual mmsec since start
unsigned long currentMillis = 0;

//Serial2 (read P1)
#define RXD2 16
#define TXD2 17

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - consumption low tariff
long mEVHT = 0; //Meter reading Electrics - consumption high tariff
long mEOLT = 0; //Meter reading Electrics - return low tariff
long mEOHT = 0; //Meter reading Electrics - return high tariff
long mEAV = 0;  //Meter reading Electrics - Actual consumption
long mEAT = 0;  //Meter reading Electrics - Actual return
long mEAV1 = 0;  //Meter reading Electrics - Actual consumption phase 1
long mEAV2 = 0;  //Meter reading Electrics - Actual consumption phase 2
long mEAV3 = 0;  //Meter reading Electrics - Actual consumption phase 3
long mEAC1 = 0;  //Meter reading Electrics - Actual current phase 1
long mEAC2 = 0;  //Meter reading Electrics - Actual current phase 2
long mEAC3 = 0;  //Meter reading Electrics - Actual current phase 3
long mGAS = 0;    //Meter reading Gas
long prevGAS = 0;
#define MAXLINELENGTH 128 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];
bool newTelegram = false;

//Send data to SQL by PHP post
const String apiKeyValue = "sfewfskjlyudgrewv32dg4";
const String sensorNameUsage = "EM01EUP";
const String sensorNameActual = "EM01EAP";
const String sensorNameGas = "EM01GU";


const char* serverNamePHP = "http://192.168.1.100/functions/esp/postMeasurement.php";
const char* serverNamePHPMulti = "http://192.168.1.100/functions/esp/postMultiMeasurement.php";

typedef struct
 {
    long dateTime;
    long mEAV;
    long mEAV1;
    long mEAV2;
    long mEAV3;
 }  udtElectricalActual;

#define MAXLOGSIZE 10
//long LogDataActual[MAXLOGSIZE][4];
udtElectricalActual LogDataActual[MAXLOGSIZE];


//NTP Time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000);

//**************************************************************************************************//
// SETUP
//**************************************************************************************************//

void setup() {
  Serial.begin(115200);
  Serial.println("*********** Setup starting ********** ");

  //Setup display
  Display_Setup();

  //Setup GPIO
  GPIO_Setup();

  //Setup Wifi
  Wifi_Setup();

  //Setup OTA (on the air update)
  OTA_Setup();

  //Setup Telnet server
  Telnet_Setup();
  
  //Setup debug client
  Debug_Setup();

  //Setup Serial port 2 (for P1 monitoring)
  SerialP1_Setup();

  //Time
  timeClient.begin();

  
  Serial.println("*********** Setup done *********** ");

}

//**************************************************************************************************//
// MAIN LOOP
//**************************************************************************************************//

void loop() {
  ArduinoOTA.handle();
  Telnet();
  Debug();


  currentMillis = millis();
  //1 second interval  
  if(currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;   
    //WifiMonitoring();
    
    timeClient.update();
    logger->println(timeClient.getEpochTime());
    logger->println(timeClient.getFormattedTime());

  }

  Display();

  //SerialP1_Read(); //Read serial port connected to P1 port
  SerialP1_Read_Decode();

}

//**************************************************************************************************//
// FUNCTIONS
//**************************************************************************************************//


//**************************************************************************************************//
// SerialP1_Setup
//**************************************************************************************************//
void SerialP1_Setup(){
  Serial.println("   **** Setup serial port 2 starting...");  
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("   **** Setup serial port 2 done...");    
}

//**************************************************************************************************//
// Display_Setup
//**************************************************************************************************//
void Display_Setup(){
  Serial.println("   **** Setup display starting...");  

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println("       SSD1306 allocation failed");
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();

  // Draw a single pixel in white
  //display.drawPixel(10, 10, SSD1306_WHITE);

  display.clearDisplay();

  display.setTextSize(1); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println(F("Welkom"));
  display.display();      // Show

  delay(2000); // Pause for 2 seconds



  Serial.println("   **** Setup display done...");    
}

//**************************************************************************************************//
// Wifi_Setup
//**************************************************************************************************//
void Wifi_Setup(){

  Serial.println("   **** Setup Wifi starting...");
    
  // Configure static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("   !!!!! Wifi.config() Failed to configure !!!! ");
  }

  // Start wifi
  WiFi.mode(WIFI_STA); //Just a station connected to wifi router
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("   Wifi connecting.....");
  }

  // Print local IP address
  Serial.println("   WiFi connected");
  Serial.print("   IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("   **** Setup Wifi done");
  Serial.println("");


}

//**************************************************************************************************//
// OTA_Setup
//**************************************************************************************************//
void OTA_Setup(){

  Serial.println("   **** Setup OTA starting...");
  
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("   !!!!! Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("   !!!!! Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("   !!!!! Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("   !!!!! Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("   !!!!! Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("   !!!!! End Failed");
    });

  ArduinoOTA.begin();

  //debug info
  Serial.println("   ATO started");
  Serial.println("   **** Setup OTA done");
  Serial.println("");


}


 


//**************************************************************************************************//
// GPIO_Setup
//**************************************************************************************************//
void GPIO_Setup(){
  // initialize digital pin ledPin as an output.
  pinMode(ledPin, OUTPUT);
}


//**************************************************************************************************//
// Debug_Setup
//**************************************************************************************************//
void Debug_Setup(){
  //Default log to serial
  logger = &Serial;
}
 

//**************************************************************************************************//
// Telnet_Setup
//
// Setup telnetserver for remote debug with telnet client
//**************************************************************************************************//
void Telnet_Setup(){
  Serial.println("   **** Setup Telnet server starting...");

  TelnetServer.begin();
  TelnetServer.setNoDelay(true);    
  
  Serial.println("   **** Setup Telnet done");
}


//**************************************************************************************************//
// Display
//
//**************************************************************************************************//
void Display(){

  static int pageNr;
  static unsigned long currentMillis;
  static unsigned long previousMillis;
  const unsigned long intervalDisplay = 10000;
  

  if(pageNr < 0 || pageNr > 3){
    pageNr = 0;
  }


  switch (pageNr) {
    case 0:
        display.clearDisplay();    
        display.setCursor(0, 0);
        display.println(F("Actueel verbruik:"));
        display.setCursor(0, 10);
        display.print("Totaal: ");
        display.print(mEAV);
        display.print(" Watt");

        display.setCursor(0, 20);
        display.print("Fase 1: ");
        display.print(mEAV1);
        display.print(" Watt");

        display.setCursor(0, 30);
        display.print("Fase 2: ");
        display.print(mEAV2);
        display.print(" Watt");

        display.setCursor(0, 40);
        display.print("Fase 3: ");
        display.print(mEAV3);
        display.print(" Watt");
              
        display.display();      // Show
      break;
    case 1:    
        display.clearDisplay();    
        display.setCursor(10, 0);
        display.println(F("Page 1"));
        display.display();      // Show   
      break;
    case 2:   
        display.clearDisplay(); 
        display.setCursor(20, 20);
        display.println(F("Page 2"));
        display.display();      // Show    
      break;
    case 3:    
        display.clearDisplay(); 
        display.setCursor(30, 40);
        display.println(F("Page 3"));
        display.display();      // Show    
      break;
  }  


  currentMillis = millis();
        
  //10 second interval
  if(currentMillis - previousMillis > intervalDisplay) {
    pageNr = pageNr + 1;

    logger->println("next page");
    previousMillis = currentMillis;   

  }

}


//**************************************************************************************************//
// Debug
//
// Select debugmode. When telnet client is connected -> debuginfo to telnet client.
// Else send debuginfo to serialport
//**************************************************************************************************//
void Debug(){
  //Use logger->println() for dynamic debug info
  //Check if telnet client is connected
  if (TelnetClient.connected()){
    logger = &TelnetClient; //Debug info to telnet
  }
  else {
    logger = &Serial; //Debug info to serial port
  }
}

//**************************************************************************************************//
// Wifi monitoring
//**************************************************************************************************//
void WifiMonitoring(){
  static bool wifiConnected;
  long wifiRSSI;

  
  if (WiFi.status() != WL_CONNECTED && wifiConnected == true) { 
    wifiConnected = false;  
    wifiErrorCount = wifiErrorCount + 1;
  }
  else {
    wifiConnected = true;
  }

  wifiRSSI = WiFi.RSSI();

  if(wifiRSSI > wifiRssiMax)
    wifiRssiMax = wifiRSSI;

  if(wifiRSSI < wifiRssiMin)
    wifiRssiMin = wifiRSSI;

  if(printWifiMonitoring == 1){
    logger->print("Wifi error count = ");
    logger->println(wifiErrorCount);
    logger->print("Wifi Strength = ");
    logger->print(wifiRSSI);
    logger->print(" , min = ");
    logger->print(wifiRssiMin);
    logger->print(" , max = ");
    logger->println(wifiRssiMax);
  }
  
}

//**************************************************************************************************//
// Telnet
//**************************************************************************************************//
void Telnet()
{
  bool ConnectionEstablished; // Flag for successfully handled connection

  //If client session reserved, but no client connected
  if (TelnetClient && !TelnetClient.connected()) {
    Serial.print("Client disconnected ... terminate session ");  
    TelnetClient.stop();
  }
  
  // Check new client connections
  if (TelnetServer.hasClient()) {
    ConnectionEstablished = false; // Set to false
     
    // find free socket
    if (!TelnetClient)
    {
      TelnetClient = TelnetServer.available(); 
        
      Serial.print("New Telnet client connected to session ");;
        
      TelnetClient.flush();  // clear input buffer, else you get strange characters

      TelnetClient.println("Welcome");
      TelnetClient.print("Millis since start: ");
      TelnetClient.println(millis());
      timeClient.update();
      TelnetClient.print("epoch time: : ");
      TelnetClient.println(timeClient.getEpochTime());

      
      TelnetClient.print("getFreeHeap: : ");
      TelnetClient.println(ESP.getFreeHeap());
      TelnetClient.print("getHeapSize: : ");
      TelnetClient.println(ESP.getHeapSize());
      TelnetClient.print("getMinFreeHeap: : ");
      TelnetClient.println(ESP.getMinFreeHeap());
      TelnetClient.print("getMaxAllocHeap: : ");
      TelnetClient.println(ESP.getMaxAllocHeap());

      TelnetClient.println("----------------------------------------------------------------");
        
      ConnectionEstablished = true; 
    }
    else {
      Serial.println("Session is in use");
   }
 
    if (ConnectionEstablished == false) {
        Serial.println("No free sessions ... drop connection");
        TelnetServer.available().stop();
    }
   }
}


//**************************************************************************************************//
// SerialP1_Read_Decode
//**************************************************************************************************//
void SerialP1_Read_Decode(){

  static unsigned long previousMillisEU; 
  static unsigned long intervalSendEU = 55000;  //Send once a minute, but telegram is send by meter every 10 sec, so allow 5 secends before a minute
  static unsigned long previousMillisGas;
  static unsigned long intervalSendGas = 250000; //Forced update interval for gas.



  if (Serial2.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (Serial2.available()) {
      int len = Serial2.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();

      //Decode telegram and check if a new complete telegram is available
      if(decodeTelegram(len+1)){
        currentMillis = millis();
        
        //60 second interval for updating usage Electra and gas.  
        if(currentMillis - previousMillisEU > intervalSendEU) {

            logger->print("EL previous = ");
            logger->println(previousMillisEU);
            logger->print("interval = ");
            logger->println(intervalSendEU);
            logger->print("current = ");
            logger->println(currentMillis);
            
            previousMillisEU = currentMillis;   

            logger->print("previous na copy = ");
            logger->println(previousMillisEU);


            //Send electra always
            SendElectraUsage();
        }

        //5 minute interval for updating gas or when new value
        if( (mGAS > prevGAS) || currentMillis - previousMillisGas > intervalSendGas){
              
          SendGasUsage();
          previousMillisGas = currentMillis;    
          prevGAS = mGAS;  
       }
     

      //Update Acutal electra as fast as possible
      SaveDataActual();
        
      } //end if decode
    } //end while
  } //end if serial2.available
}


//**************************************************************************************************//
// SaveDataActual
//
// Save data in a array (que or shift register).
// When new data is available, shift all data one position. 
// Add new data to position 0
// When the complete array is full, send data to SQL server and empty array.
//**************************************************************************************************//
void SaveDataActual(){

  long i;

  //Shift all data one position
  for( i = MAXLOGSIZE; i >= 1; i--){
    LogDataActual[i] = LogDataActual[i-1];  
  }

  //Update record 0 with new data
  timeClient.update();
  LogDataActual[0].dateTime =  timeClient.getEpochTime();
  LogDataActual[0].mEAV =  mEAV;
  LogDataActual[0].mEAV1 =  mEAV1;
  LogDataActual[0].mEAV2 =  mEAV2;
  LogDataActual[0].mEAV3 =  mEAV3;

  //Print debug info
  if( printElectrActualQueue){
    for( i = 0; i <= MAXLOGSIZE; i++){
      logger->print(i);
      logger->print(" = ");
      logger->println(LogDataActual[i].dateTime);
      logger->print(" ; ");
      logger->println(LogDataActual[i].mEAV);
    }
  }
  
  //Check if array is full
  if( LogDataActual[MAXLOGSIZE].dateTime > 0 ) {
    logger->println("Sending data.....");    
    SendMultiElectraActual();

    //Empty array
    for( i = MAXLOGSIZE; i >= 0; i--){
      LogDataActual[i] = (udtElectricalActual) {0,0,0,0,0};;  
    }

  }

}


//**************************************************************************************************//
// SendMultiElectraActual
//**************************************************************************************************//
void SendMultiElectraActual(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHPMulti);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
    // Prepare your HTTP POST request data
    String httpRequestData = "api_key=" + apiKeyValue 

                          + "&sensorId=" + sensorNameActual
                          
                          + "&datetime_0=" + LogDataActual[9].dateTime
                          + "&value1_0=" + LogDataActual[9].mEAV
                          + "&value2_0=" + LogDataActual[9].mEAV1
                          + "&value3_0=" + LogDataActual[9].mEAV2 
                          + "&value4_0=" + LogDataActual[9].mEAV3 

                          + "&datetime_1=" + LogDataActual[8].dateTime
                          + "&value1_1=" + LogDataActual[8].mEAV
                          + "&value2_1=" + LogDataActual[8].mEAV1
                          + "&value3_1=" + LogDataActual[8].mEAV2 
                          + "&value4_1=" + LogDataActual[8].mEAV3 
                          
                          + "&datetime_2=" + LogDataActual[7].dateTime
                          + "&value1_2=" + LogDataActual[7].mEAV
                          + "&value2_2=" + LogDataActual[7].mEAV1
                          + "&value3_2=" + LogDataActual[7].mEAV2 
                          + "&value4_2=" + LogDataActual[7].mEAV3 

                          + "&datetime_3=" + LogDataActual[6].dateTime
                          + "&value1_3=" + LogDataActual[6].mEAV
                          + "&value2_3=" + LogDataActual[6].mEAV1
                          + "&value3_3=" + LogDataActual[6].mEAV2 
                          + "&value4_3=" + LogDataActual[6].mEAV3 

                          + "&datetime_4=" + LogDataActual[5].dateTime
                          + "&value1_4=" + LogDataActual[5].mEAV
                          + "&value2_4=" + LogDataActual[5].mEAV1
                          + "&value3_4=" + LogDataActual[5].mEAV2 
                          + "&value4_4=" + LogDataActual[5].mEAV3 

                          + "&datetime_5=" + LogDataActual[4].dateTime
                          + "&value1_5=" + LogDataActual[4].mEAV
                          + "&value2_5=" + LogDataActual[4].mEAV1
                          + "&value3_5=" + LogDataActual[4].mEAV2 
                          + "&value4_5=" + LogDataActual[4].mEAV3 

                          + "&datetime_6=" + LogDataActual[3].dateTime
                          + "&value1_6=" + LogDataActual[3].mEAV
                          + "&value2_6=" + LogDataActual[3].mEAV1
                          + "&value3_6=" + LogDataActual[3].mEAV2 
                          + "&value4_6=" + LogDataActual[3].mEAV3 

                          + "&datetime_7=" + LogDataActual[2].dateTime
                          + "&value1_7=" + LogDataActual[2].mEAV
                          + "&value2_7=" + LogDataActual[2].mEAV1
                          + "&value3_7=" + LogDataActual[2].mEAV2 
                          + "&value4_7=" + LogDataActual[2].mEAV3 

                          + "&datetime_8=" + LogDataActual[1].dateTime
                          + "&value1_8=" + LogDataActual[1].mEAV
                          + "&value2_8=" + LogDataActual[1].mEAV1
                          + "&value3_8=" + LogDataActual[1].mEAV2 
                          + "&value4_8=" + LogDataActual[1].mEAV3 

                          + "&datetime_9=" + LogDataActual[0].dateTime
                          + "&value1_9=" + LogDataActual[0].mEAV
                          + "&value2_9=" + LogDataActual[0].mEAV1
                          + "&value3_9=" + LogDataActual[0].mEAV2 
                          + "&value4_9=" + LogDataActual[0].mEAV3 

                          + "";

    /*
    String httpRequestData = "api_key=sfewfskjlyudgrewv32dg4&sensorId=EM01EAP";
                          httpRequestData += "&datetime_0=" + LogDataActual[9].dateTime;
                          httpRequestData += "&value1_0=" + LogDataActual[9].mEAV;
                          httpRequestData += "&value2_0=" + LogDataActual[9].mEAV1;
                          httpRequestData += "&value3_0=" + LogDataActual[9].mEAV2; 
                          httpRequestData += "&value4_0=" + LogDataActual[9].mEAV3; 

                          httpRequestData += "&datetime_1=" + LogDataActual[8].dateTime;
                          httpRequestData += "&value1_1=" + LogDataActual[8].mEAV;
                          httpRequestData += "&value2_1=" + LogDataActual[8].mEAV1;
                          httpRequestData += "&value3_1=" + LogDataActual[8].mEAV2; 
                          httpRequestData += "&value4_1=" + LogDataActual[8].mEAV3; 
                          
                          httpRequestData += "&datetime_2=" + LogDataActual[7].dateTime;
                          httpRequestData += "&value1_2=" + LogDataActual[7].mEAV;
                          httpRequestData += "&value2_2=" + LogDataActual[7].mEAV1;
                          httpRequestData += "&value3_2=" + LogDataActual[7].mEAV2; 
                          httpRequestData += "&value4_2=" + LogDataActual[7].mEAV3; 

                          httpRequestData += "&datetime_3=" + LogDataActual[6].dateTime;
                          httpRequestData += "&value1_3=" + LogDataActual[6].mEAV;
                          httpRequestData += "&value2_3=" + LogDataActual[6].mEAV1;
                          httpRequestData += "&value3_3=" + LogDataActual[6].mEAV2; 
                          httpRequestData += "&value4_3=" + LogDataActual[6].mEAV3; 

                          httpRequestData += "&datetime_4=" + LogDataActual[5].dateTime;
                          httpRequestData += "&value1_4=" + LogDataActual[5].mEAV;
                          httpRequestData += "&value2_4=" + LogDataActual[5].mEAV1;
                          httpRequestData += "&value3_4=" + LogDataActual[5].mEAV2; 
                          httpRequestData += "&value4_4=" + LogDataActual[5].mEAV3; 

                          httpRequestData += "&datetime_5=" + LogDataActual[4].dateTime;
                          httpRequestData += "&value1_5=" + LogDataActual[4].mEAV;
                          httpRequestData += "&value2_5=" + LogDataActual[4].mEAV1;
                          httpRequestData += "&value3_5=" + LogDataActual[4].mEAV2; 
                          httpRequestData += "&value4_5=" + LogDataActual[4].mEAV3; 

                          httpRequestData += "&datetime_6=" + LogDataActual[3].dateTime;
                          httpRequestData += "&value1_6=" + LogDataActual[3].mEAV;
                          httpRequestData += "&value2_6=" + LogDataActual[3].mEAV1;
                          httpRequestData += "&value3_6=" + LogDataActual[3].mEAV2; 
                          httpRequestData += "&value4_6=" + LogDataActual[3].mEAV3; 

                          httpRequestData += "&datetime_7=" + LogDataActual[2].dateTime;
                          httpRequestData += "&value1_7=" + LogDataActual[2].mEAV;
                          httpRequestData += "&value2_7=" + LogDataActual[2].mEAV1;
                          httpRequestData += "&value3_7=" + LogDataActual[2].mEAV2; 
                          httpRequestData += "&value4_7=" + LogDataActual[2].mEAV3; 

                          httpRequestData += "&datetime_8=" + LogDataActual[1].dateTime;
                          httpRequestData += "&value1_8=" + LogDataActual[1].mEAV;
                          httpRequestData += "&value2_8=" + LogDataActual[1].mEAV1;
                          httpRequestData += "&value3_8=" + LogDataActual[1].mEAV2 ;
                          httpRequestData += "&value4_8=" + LogDataActual[1].mEAV3; 

                          httpRequestData += "&datetime_9=" + LogDataActual[9].dateTime;
                          httpRequestData += "&value1_9=" + LogDataActual[0].mEAV;
                          httpRequestData += "&value2_9=" + LogDataActual[0].mEAV1;
                          httpRequestData += "&value3_9=" + LogDataActual[0].mEAV2; 
                          httpRequestData += "&value4_9=" + LogDataActual[0].mEAV3; 

                         // + "";
*/

   
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    //Check response code    
    if (httpResponseCode>0) {
      if(printHTTPClient){
        logger->print("HTTP Response code Electra actual multi: ");
        logger->println(httpResponseCode);
        logger->println(httpRequestData);
      }
    }
    else {
      logger->print("Error code Electra actual mulit: ");
      logger->println(httpResponseCode);
      logger->println(httpRequestData);
    }
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}




//**************************************************************************************************//
// SendGasUsage
//**************************************************************************************************//
void SendGasUsage(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Prepare your HTTP POST request data
    String httpRequestData = "api_key=" + apiKeyValue 
                          + "&sensorId=" + sensorNameGas
                          + "&value1=" + mGAS
                          + "&value2=" + "0" 
                          + "&value3=" + "0" 
                          + "&value4=" + "0" + "";

    /*
    String httpRequestData = "api_key=sfewfskjlyudgrewv32dg4&sensorId=EM01GU&value1=" + mGAS;
                          httpRequestData += "&value2=0"; 
                          httpRequestData += "&value3=0";
                          httpRequestData += "&value4=0";
*/
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    //Check response code    
    if (httpResponseCode>0) {
      if(printHTTPClient){
        logger->print("HTTP Response code Gas Usage: ");
        logger->println(httpResponseCode);
      }
    }
    else {
      logger->print("Error code Gas Usage: ");
      logger->println(httpResponseCode);
      logger->println(httpRequestData);
    }
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}




//**************************************************************************************************//
// SendElectraUsage
//**************************************************************************************************//
void SendElectraUsage(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    long mEVTT = mEVLT + mEVHT;

    // Prepare your HTTP POST request data
    String httpRequestData = "api_key=" + apiKeyValue 
                          + "&sensorId=" + sensorNameUsage
                          + "&value1=" + mEVTT
                          + "&value2=" + mEVLT 
                          + "&value3=" + mEVHT 
                          + "&value4=" + "0" + "";
    /*
    String httpRequestData = "api_key=sfewfskjlyudgrewv32dg4"; 
            httpRequestData += "&sensorId=EM01EUP";
            httpRequestData += "&value1=" + mEVTT;
            httpRequestData += "&value2=" + mEVLT ;
            httpRequestData += "&value3=" + mEVHT ;
            httpRequestData += "&value4=0";
*/
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    //Check response code    
    if (httpResponseCode>0) {
      if(printHTTPClient){
        logger->print("HTTP Response code electra usage: ");
        logger->println(httpResponseCode);
      }
    }
    else {
      logger->print("Error code electra usage: ");
      logger->println(httpResponseCode);
      logger->println(httpRequestData);
    }
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}




//**************************************************************************************************//
// decodeTelegram
//**************************************************************************************************//
bool decodeTelegram(int len) {
 
  //Check if we start reading from the first line of the telegram
  if (strncmp(telegram, "/KFM5KAIFA-METER", strlen("/KFM5KAIFA-METER")) == 0) 
    newTelegram =  true;

  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0) 
    mEVLT =  getValue(telegram, len);

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0) 
    mEVHT = getValue(telegram, len);


  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) 
    mEOLT = getValue(telegram, len);

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) 
    mEOHT = getValue(telegram, len);

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) 
    mEAV = getValue(telegram, len);
  
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0) 
    mEAT = getValue(telegram, len);

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (strncmp(telegram, "0-1:24.2.1", strlen("0-1:24.2.1")) == 0) 
    mGAS = getValue(telegram, len);

  // 1-0:31.7.0 = Ampere phase 1
  if (strncmp(telegram, "1-0:31.7.0", strlen("1-0:31.7.0")) == 0) 
    mEAC1 = getValue(telegram, len);

  // 1-0:51.7.0 = Ampere phase 2
  if (strncmp(telegram, "1-0:51.7.0", strlen("1-0:51.7.0")) == 0) 
    mEAC2 = getValue(telegram, len);

  // 1-0:71.7.0 = Ampere phase 3
  if (strncmp(telegram, "1-0:71.7.0", strlen("1-0:71.7.0")) == 0) 
    mEAC3 = getValue(telegram, len);

  // 1-0:21.7.0 = Actual power phase 1
  if (strncmp(telegram, "1-0:21.7.0", strlen("1-0:21.7.0")) == 0) 
    mEAV1 = getValue(telegram, len);

  // 1-0:41.7.0 = Actual power phase 1
  if (strncmp(telegram, "1-0:41.7.0", strlen("1-0:41.7.0")) == 0)
    mEAV2 = getValue(telegram, len);
  
  // 1-0:61.7.0 = Actual power phase 1
  if (strncmp(telegram, "1-0:61.7.0", strlen("1-0:61.7.0")) == 0) 
    mEAV3 = getValue(telegram, len);


  //Check for last line in telegram
  if ((strncmp(telegram, "!", strlen("!")) == 0) && newTelegram == true) {
    if(printSerialP1decoded){
      logger->println("------------ New data from P1 ------------");
      logger->print("mEVLT (verbruik laag) is ");
      logger->println(mEVLT);    
      logger->print("mEVHT (verbruik hoog) is ");
      logger->println(mEVHT);    
      //logger->print("mEOLT (opbrengst laag) is ");
      //logger->println(mEOLT); 
      //logger->print("mEOHT (opbrengst hoog) is ");
      //logger->println(mEOHT);   
      logger->print("mEAV (actueel verbruik) is ");
      logger->println(mEAV);     
      //logger->print("mEAT (actueel teruglevering) is ");
      //logger->println(mEAT);      
      logger->print("mGAS (gas) is ");
      logger->println(mGAS);  
      //logger->print("mEAC1 (Actueel ampres phase 1) is ");
      //logger->println(mEAC1); 
      //logger->print("mEAC2 (Actueel ampres phase 2) is ");
      //logger->println(mEAC2); 
      //logger->print("mEAC3 (Actueel ampres phase 3) is ");
      //logger->println(mEAC3);    
      logger->print("mEAV1 (acuteel kw phase 1) is ");
      logger->println(mEAV1);        
      logger->print("mEAV2 (acuteel kw phase 2) is ");
      logger->println(mEAV2);      
      logger->print("mEAV3 (acuteel kw phase 3) is ");
      logger->println(mEAV3);  
   
    }
    
    newTelegram = false;
    return true;
  }

  
  return false;
/* todo:
0-0:96.7.21(00024)  -- number of power failures
0-0:96.7.9(00015) --  number of long power failures in any phase
1-0:99.97.0(10)(0-0:96.7.19)(190806095803S)(0000029797*s)(190801071827S)(0000024028*s)(180502135944S)(0000003425*s)(180419181056S)(0000003342*s)(160708153833S)(0000002257*s)(160619153745S)(0000000617*s)(160212145126W)(0000002956*s)(160211145829W)(0000003188*s)(160126133520W)(0000000255*s)(000101000002W)(2147483647*s)
                  -- power failure event log (long power fail). Timestamp (end time - duration in seconds)
1-0:32.32.0(00007)  -- number of voltage sags in phase L1
1-0:52.32.0(00001)  -- number of voltage sags in phase L2
1-0:72.32.0(00001)  -- number of voltage sags in phase L3
1-0:32.36.0(00000)  -- number of voltage swels in phase L1
1-0:52.36.0(00000)  -- number of voltage swels in phase L2
1-0:72.36.0(00000)  -- number of voltage swels in phase L3
*/

}




//**************************************************************************************************//
// isNumber
//**************************************************************************************************//
bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}


//**************************************************************************************************//
// FindCharInArrayRev
//**************************************************************************************************//
int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }

  return -1;
}


//**************************************************************************************************//
// getValidVal
//**************************************************************************************************//
long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}


//**************************************************************************************************//
// getValue
//**************************************************************************************************//
long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;

  char res[16];

  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }

  return 0;

}



/* Example data:

/KFM5KAIFA-METER

1-3:0.2.8(42)     --  Version information
0-0:1.0.0(191113185946W)  --  Date-time of message YYMMDDhhmmssX
0-0:96.1.1(4530303236303030303133303431363134)    --  Equipment identifier
1-0:1.8.1(003841.425*kWh)   -- Delivered to client tariff 1
1-0:1.8.2(005771.280*kWh)   -- Delivered to client tariff 2
1-0:2.8.1(000000.000*kWh)   -- Delivered by client tariff 2
1-0:2.8.2(000000.000*kWh)   -- Delivered by client tariff 2
0-0:96.14.0(0002)   --  tariff inidcator
1-0:1.7.0(00.259*kW)  --  acutal power delivered to client
1-0:2.7.0(00.000*kW)  --  actual power delivered by client
0-0:96.7.21(00024)  -- number of power failures
0-0:96.7.9(00015) --  number of long power failures in any phase
1-0:99.97.0(10)(0-0:96.7.19)(190806095803S)(0000029797*s)(190801071827S)(0000024028*s)(180502135944S)(0000003425*s)(180419181056S)(0000003342*s)(160708153833S)(0000002257*s)(160619153745S)(0000000617*s)(160212145126W)(0000002956*s)(160211145829W)(0000003188*s)(160126133520W)(0000000255*s)(000101000002W)(2147483647*s)
                  -- power failure event log (long power fail). Timestamp (end time - duration in seconds)
1-0:32.32.0(00007)  -- number of voltage sags in phase L1
1-0:52.32.0(00001)  -- number of voltage sags in phase L2
1-0:72.32.0(00001)  -- number of voltage sags in phase L3
1-0:32.36.0(00000)  -- number of voltage swels in phase L1
1-0:52.36.0(00000)  -- number of voltage swels in phase L2
1-0:72.36.0(00000)  -- number of voltage swels in phase L3
0-0:96.13.1()   -- ????
0-0:96.13.0()   --  Text message max 1024 char
1-0:31.7.0(000*A)   -- Instantaneous current L1 (Amp)
1-0:51.7.0(000*A)   -- Instantaneous current L2 (Amp)
1-0:71.7.0(000*A)   -- Instantaneous current L3 (Amp)
1-0:21.7.0(00.033*kW)   -- Instantaneous actual +power L1 (kW)
1-0:22.7.0(00.000*kW)   -- Instantaneous actual -power L1 (kW)
1-0:41.7.0(00.096*kW)   -- Instantaneous actual +power L2 (kW)
1-0:42.7.0(00.000*kW)   -- Instantaneous actual -power L2 (kW)
1-0:61.7.0(00.131*kW)   -- Instantaneous actual +power L3 (kW)
1-0:62.7.0(00.000*kW)   -- Instantaneous actual -power L3 (kW)
0-1:24.1.0(003)   -- Device type
0-1:96.1.0(4730303139333430323631393730393135)  --Equipment identifier
0-1:24.2.1(191113180000W)(03188.841*m3)   -- Last 5 minute meter reading in 0,001 m3
!02C8
*/



/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/
/***********************************************************************************************************************/

/*********************  OLD FUNCTIONS ********************/

//**************************************************************************************************//
// SerialP1_Read
//**************************************************************************************************//
/*
void SerialP1_Read(){
  //Read serial port 2  (for P1 monitoring)
    
  if (Serial2.available()) {
    char c;

    while(Serial2.available()) {
      digitalWrite(ledPin, HIGH);
      c = char(Serial2.read());

      if(printSerial2 == 1){
        logger->print(c);  
      }
    }
  }
}
*/
//**************************************************************************************************//
// SendElectraActual
//**************************************************************************************************//
/*
void SendElectraActual(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Prepare your HTTP POST request data
    String httpRequestData = "api_key=sfewfskjlyudgrewv32dg4" ;
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    //Check response code    
    if (httpResponseCode>0) {
      if(printHTTPClient){
        logger->println(httpRequestData);
        logger->print("HTTP Response code: ");
        logger->println(httpResponseCode);
      }
    }
    else {
      logger->print("Error code: ");
      logger->println(httpResponseCode);
    }
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}
*/
