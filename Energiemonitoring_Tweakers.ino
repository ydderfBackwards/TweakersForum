/* TODO
 *  Use second core for display
  *  Show bargraph on display
 *  Display page can be changed by botton (not pressed for x-time is auto scroll, press is next page)
 *  Prevent overflow currenttime / millis()
 *  Show error info from P1
 *  Add error led (on for 10 seconds after an error)
 *  Combine functions for sending to sql
 *  Log error time wifi (check if works)
 *  Show info on website
 *  Show water on display
 *  At startup, check if watersensor is active (if so, don't count?)
 *  Watermeter pulsen beveiligen. 7 jan rond tussen 18 en 19u zijn er 4000 pulsen geweest in één minuut
 *  */

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
#define printWifiMonitoring 0
#define printSerialP1Raw 0
#define printSerialP1decoded 1
#define printHTTPClient 1
#define printElectrActualQueue 0
#define printWaterMeter 1
#define printCycleTime 1
#define printbuttonDisplay 1


//Display OLED with SSD1306 chip
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


//Wifi
const char* ssid = "SECRET";
const char* password = "WELKOM01";
IPAddress local_IP(192, 168, 0, 150);
IPAddress gateway(192, 168, 0, 254);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   
IPAddress secondaryDNS(8, 8, 4, 4); 

//Wifi monitoring
long wifiErrorCount = 0; //Start with no errors
long wifiRssiMax = -9999; //Start with a very low max, so the first reading will always be more.
long wifiRssiMin = 9999; //Start with a very high min, so the first reading will always be less.
long wifiRSSI;

//Logging of errors
typedef struct
 {
    String dateTime = "No data";
    long duration = 0;
 }  udtWifiErrorLogging;

#define MAXLOGSIZEWIFILOG 5
udtWifiErrorLogging LogDataWifiErrors[MAXLOGSIZEWIFILOG];

//Telnet
WiFiServer TelnetServer(23); //Telnet on default port 23
WiFiClient TelnetClient;

//Debug
Print* logger; //Logger will be used for printing to Serial or Telnet monitor.

//GPIO 
const int waterPin = 4;
const int buttonDisplayPin = 5;

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
#define MAXLINELENGTH 400//128 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];
bool newTelegram = false;

//Send data to SQL by PHP post
const char* apiKeyValue = "123456789";
const char* sensorNameUsage = "EM01EUP";
const char* sensorNameActual = "EM01EAP";
const char* sensorNameGas = "EM01GU";
const char* sensorNameWater = "EM01WU";

const char* serverNamePHP = "http://192.168.0.180/functions/postMeasurement.php";
const char* serverNamePHPMulti = "http://192.168.0.180/functions/postMultiMeasurement.php";

const char* serverNamePHPWater = "http://192.168.0.180/functions/getLastWaterValue.php";

//Logging of electrical actual usage
typedef struct
 {
    long dateTime;
    long mEAV;
    long mEAV1;
    long mEAV2;
    long mEAV3;
 }  udtElectricalActual;

#define MAXLOGSIZE 10
udtElectricalActual LogDataActual[MAXLOGSIZE];

//Puls sensor water meter
typedef struct
 {
    bool activeUnfiltered = false;
    bool active = false;
    bool lastActiveUnfiltered = false;
    bool lastActive = false;
    bool fp_active = false;
    bool fn_active = false;
    unsigned long filterTime = 500;
    unsigned long actPosFilterTime = 0;
    unsigned long actNegFilterTime = 0;
 }  udtPulsSensor;

udtPulsSensor waterMeter;

//WaterMeter
long mWAV = 0; //Water Actual Value
const long pulsFactorWater = 1; //one pulse is one liter

//Button display
udtPulsSensor buttonDisplay;


//NTP Time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000);

//Cycletime monitoring
unsigned long cycleTimeMax = 0;
unsigned long cycleTimeMin = 999;
unsigned long cycleTimeAvg = 0;
#define MAXLOGSIZECYCLETIME 20

//General
String startupDateTime = "";

/**************************************************************************************************
* SETUP
**************************************************************************************************/

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

  //Watermeter
  WaterMeter_Setup();


  //Log startup time
  startupDateTime = GetFullFormattedTime();
  
  Serial.println("*********** Setup done *********** ");

}

/**************************************************************************************************
* MAIN LOOP
**************************************************************************************************/

void loop() {
  static unsigned long previousMillis = 0;        // will store last time LED was updated
  const unsigned long interval = 10000;           // interval at which to blink (milliseconds)
  int i;
  
  ArduinoOTA.handle();
  Telnet();
  Debug();


  currentMillis = millis();
  //1 second interval  
  if(currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;   
    
    WifiMonitoring();

    
    if(printCycleTime){
      logger->print("Cycletime AVG: ");
      logger->print(cycleTimeAvg);
      logger->println(" microSec");
      logger->print("Cycletime MIN: ");
      logger->print(cycleTimeMin);
      logger->println(" microSec");
      logger->print("Cycletime Max: ");
      logger->print(cycleTimeMax);
      logger->println(" microSec");
    }

      
    
  }

  Display();

  //SerialP1_Read(); //Read serial port connected to P1 port
  SerialP1_Read_Decode();

  //Pulses from watermeter
  WaterMeter();

  //Monitor cycle time
  MonitorCycleTime();
}

/**************************************************************************************************
* FUNCTIONS
**************************************************************************************************/


/**************************************************************************************************
* SerialP1_Setup
**************************************************************************************************/
void SerialP1_Setup(){
  Serial.println("   **** Setup serial port 2 starting...");  
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("   **** Setup serial port 2 done...");    
}

/**************************************************************************************************
* Display_Setup
**************************************************************************************************/
void Display_Setup(){
  Serial.println("   **** Setup display starting...");  

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println("       SSD1306 allocation failed");
    //for(;;); // Don't proceed, loop forever
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

/**************************************************************************************************
* Wifi_Setup
**************************************************************************************************/
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

/**************************************************************************************************
* OTA_Setup
**************************************************************************************************/
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


 


/**************************************************************************************************
* GPIO_Setup
**************************************************************************************************/
void GPIO_Setup(){
  // initialize digital pin as input.
  pinMode(waterPin, INPUT);
  pinMode(buttonDisplayPin, INPUT);
}


/**************************************************************************************************
* Debug_Setup
**************************************************************************************************/
void Debug_Setup(){
  //Default log to serial
  logger = &Serial;
}
 

/**************************************************************************************************
* Telnet_Setup
*
* Setup telnetserver for remote debug with telnet client
**************************************************************************************************/
void Telnet_Setup(){
  Serial.println("   **** Setup Telnet server starting...");

  TelnetServer.begin();
  TelnetServer.setNoDelay(true);    
  
  Serial.println("   **** Setup Telnet done");
}


/**************************************************************************************************
* WaterMeter_Setup
* 
* Request actual meter value from SQL.
* Use this value as initial counter value in the mainloop
**************************************************************************************************/
void WaterMeter_Setup(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHPWater);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Prepare your HTTP POST request data
    char* httpRequestData;
    httpRequestData = (char *)malloc(sizeof(char) * 10);

    if(httpRequestData != NULL )
    {
      
      strcpy (httpRequestData, ""); //No parameters to send
      
      // Send HTTP POST request
      int httpResponseCode = http.POST(httpRequestData);
  
      //Check response code    
      if (httpResponseCode>0) {

        //Get value
        mWAV = http.getString().toInt();
        
        if(printHTTPClient){
          logger->print("HTTP Response code Last Water value: ");
          logger->println(httpResponseCode);
          logger->print("Last Water Value mWAV = ");
          logger->println(mWAV);
        }

      }
      else {
        logger->print("Error code Last Water value: ");
        logger->println(httpResponseCode);
        logger->println(httpRequestData);
      }

    }
    else{
      logger->println("Failed to allocate memory");
    }

    //Free memory
    free(httpRequestData);
   
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}



/**************************************************************************************************
* GetFullFormattedTime
* 
* Reads actual epoch time and convert this to UTC time: yyyy:mm:dd hh:mm
**************************************************************************************************/
String GetFullFormattedTime() {

   //Get actual time
   timeClient.update();
   time_t rawtime =  timeClient.getEpochTime();
   struct tm * ti;
   //ti = localtime (&rawtime);
   ti = gmtime (&rawtime); //ESP doesn't get correct timezone, so use UTC

  
   //Convert Epoch time to UTC and format a string yyyy:mm:dd hh:mm
   uint16_t year = ti->tm_year + 1900;
   String yearStr = String(year);

   uint8_t month = ti->tm_mon + 1;
   String monthStr = month < 10 ? "0" + String(month) : String(month);

   uint8_t day = ti->tm_mday;
   String dayStr = day < 10 ? "0" + String(day) : String(day);

   uint8_t hours = ti->tm_hour;
   String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);

   uint8_t minutes = ti->tm_min;
   String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);

   uint8_t seconds = ti->tm_sec;
   String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);

   return yearStr + "-" + monthStr + "-" + dayStr + " " + hoursStr + ":" + minuteStr + ":" + secondStr;
}


/**************************************************************************************************
* WaterMeter
* 
* Reads digital input with a filter
* Create positive and negative flank of filtered digital input
* Every positive flank the puls factuor will be added to the actual counter value
* Every 60 seconds, the counter value will be send to SQL
**************************************************************************************************/
void WaterMeter(){
    unsigned long currentMillis = 0;
    unsigned long currentMillisWater = millis();

    static unsigned long previousMillisWAV = 0; 
    const unsigned long intervalSendWAV = 60000;


    //Read input
    waterMeter.activeUnfiltered = digitalRead(waterPin);

    //Reset signals.
    waterMeter.fp_active = false;
    waterMeter.fn_active = false;

    //Check if input is same as last scan
    if(waterMeter.lastActiveUnfiltered != waterMeter.activeUnfiltered){
      //Input has changed --> reset timers
      waterMeter.actPosFilterTime = currentMillisWater;
      waterMeter.actNegFilterTime = currentMillisWater;
    }
    
    if(waterMeter.activeUnfiltered){
        //Check if filter time is done
        if(currentMillisWater - waterMeter.actPosFilterTime > waterMeter.filterTime) {
          waterMeter.active = true;

        
          if(waterMeter.lastActive == false){
            waterMeter.fp_active = true;

                if( printWaterMeter ){
                  logger->println("WaterMeter flank Positive.");    
                }
          }
       }
    }
    else{
        //Check if filter time is done
        if(currentMillisWater - waterMeter.actNegFilterTime > waterMeter.filterTime) {
          waterMeter.active = false;

          if(waterMeter.lastActive == true){
            waterMeter.fn_active = true;
            
                if( printWaterMeter ){
                  logger->println("WaterMeter flank Negative.");    
                }
          }
       }      
    }

    //Save actual state for next scan.
    waterMeter.lastActiveUnfiltered = waterMeter.activeUnfiltered; 
    waterMeter.lastActive = waterMeter.active; 

    //If new puls from meter
    if( waterMeter.fp_active ){
      mWAV = mWAV + pulsFactorWater;
    }




    currentMillis = millis();
        
    //60 second interval for updating usage water.  
    if(currentMillis - previousMillisWAV > intervalSendWAV) {
        
        previousMillisWAV = currentMillis;   

        //Check if we have a valid value;
        if( mWAV > 100 ){
          //Send water
          SendWaterUsage();  
        }
        else{
          //Request again the last value from sql database
          WaterMeter_Setup();
        }
        
    }



}


/**************************************************************************************************
* Display
* 
* Shows info on display. 
* The shown page will change automatically every 10 seconds
**************************************************************************************************/
void Display(){

  static int pageNr = 0;
  static int pageNrPrevious = 0;
  static unsigned long currentMillis; //Will be initialezed in code
  static unsigned long previousMillis = millis();
  static unsigned long previousMillisUpdate = millis();
  const unsigned long intervalDisplay = 10000;      //Show next page
  const unsigned long intervalDisplayUpdate = 1000; //Update values

  //Shorter time for a button
  buttonDisplay.filterTime = 100;


  //Read current millis
  currentMillis = millis();
 

  //Read input
  buttonDisplay.activeUnfiltered = digitalRead(buttonDisplayPin);

  //Reset signals.
  buttonDisplay.fp_active = false;
  buttonDisplay.fn_active = false;

  //Check if input is same as last scan
  if(buttonDisplay.lastActiveUnfiltered != buttonDisplay.activeUnfiltered){
    //Input has changed --> reset timers
    buttonDisplay.actPosFilterTime = currentMillis;
    buttonDisplay.actNegFilterTime = currentMillis;
  }

  //Check if input is active
  if(buttonDisplay.activeUnfiltered){
      //Check if filter time is done
      if(currentMillis - buttonDisplay.actPosFilterTime > buttonDisplay.filterTime) {
        //Button is active long enough
        buttonDisplay.active = true;

        //Check if button was active previous scan.
        if(buttonDisplay.lastActive == false){
          buttonDisplay.fp_active = true;

              if( printbuttonDisplay ){
                logger->println("buttonDisplay flank Positive.");    
              }
        }
     }
  }
  else{
      //Check if filter time is done
      if(currentMillis - buttonDisplay.actNegFilterTime > buttonDisplay.filterTime) {
        buttonDisplay.active = false;

        if(buttonDisplay.lastActive == true){
          buttonDisplay.fn_active = true;
          
              if( printbuttonDisplay ){
                logger->println("buttonDisplay flank Negative.");    
              }
        }
     }      
  }

  //Save actual state for next scan.
  buttonDisplay.lastActiveUnfiltered = buttonDisplay.activeUnfiltered; 
  buttonDisplay.lastActive = buttonDisplay.active; 

    
  //Check if button pressed
  if( buttonDisplay.fp_active){
    //Show next page
    pageNr = pageNr + 1;

    //Block automatic showing next page for 60 seconds
    previousMillis = currentMillis + 60000;
  }


        
  //10 second interval for showing next page
  if(currentMillis - previousMillis > intervalDisplay) {
    pageNr = pageNr + 1;
    previousMillis = currentMillis;   
  }

  //Check if selected page is within limits
  if(pageNr < 0 || pageNr > 3){
    pageNr = 0;
  }

  //Update display
  if( pageNr != pageNrPrevious || (currentMillis - previousMillisUpdate > intervalDisplayUpdate)){
    
    //Always clear display before writing new lines
    display.clearDisplay();      

    //Show selected page
    switch (pageNr) {
      case 0:
 
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
                
        break;
      case 1:  
          display.setCursor(0, 0);  
          display.println(F("Meterstand:"));
          display.setCursor(0, 10);
          display.print("Totaal: ");
          display.print(mEVLT + mEVHT);
          display.print(" kWh");
  
          display.setCursor(0, 20);
          display.print("Laag: ");
          display.print(mEVLT);
          display.print(" kWh");
  
          display.setCursor(0, 30);
          display.print("Hoog: ");
          display.print(mEVHT);
          display.print(" kWh");
  
          display.setCursor(0, 40);
          display.print("Gas: ");
          display.print(mGAS);
          display.print(" m3");

          display.setCursor(0, 50);
          display.print("Water: ");
          display.print(mWAV);
          display.print(" l");

          break;
      case 2:    
          display.setCursor(0, 0);
          display.print(F("IP: "));
          display.print(WiFi.localIP());
          
          display.setCursor(0, 10);
          display.print("Errorcount : ");
          display.print(wifiErrorCount);
  
          display.setCursor(0, 20);
          display.print("Rssi act: ");
          display.print(wifiRSSI);
          display.print(" dBm");
  
          display.setCursor(0, 30);
          display.print("Rssi min: ");
          display.print(wifiRssiMin);
          display.print(" dBm");
  
          display.setCursor(0, 40);
          display.print("Rssi max: ");
          display.print(wifiRssiMax);
          display.print(" dBm");
          
        break;
      case 3:    
          display.setCursor(0, 0);
          display.print(F("Error log:"));
                    
          display.setCursor(0, 10);
          display.print(LogDataWifiErrors[0].dateTime);
          display.print(" : ");
          display.print(LogDataWifiErrors[0].duration);
  
          display.setCursor(0, 20);
          display.print(LogDataWifiErrors[1].dateTime);
          display.print(" : ");
          display.print(LogDataWifiErrors[1].duration);
  
          display.setCursor(0, 30);
          display.print(LogDataWifiErrors[2].dateTime);
          display.print(" : ");
          display.print(LogDataWifiErrors[2].duration);
  
          display.setCursor(0, 40);
          display.print(LogDataWifiErrors[3].dateTime);
          display.print(" : ");
          display.print(LogDataWifiErrors[3].duration);
          
        break;

    }
    display.display();      // Show

    pageNrPrevious = pageNr;
    previousMillisUpdate = currentMillis;   
  }


}


/**************************************************************************************************
* MonitorCycleTime
*
* Monitor actual cycle time of main loop. 
* This is done in Micro seconds (not miliseconds) because the cycle time is below a milisecond
* The minimal and maximal value (since startup) are logged.
* The actual cycle time is logged in a small buffer.
* The average cycle time is calculated over all values in this small buffer
* The first call of this function, the MIN and MAX will not be calculated, because they are not correct (setup function takes also time)
**************************************************************************************************/
void MonitorCycleTime(){
  static unsigned long cycleTimeLog[MAXLOGSIZECYCLETIME];
  long i;
  static unsigned long lastMicroSec = 0;
  unsigned long actMicroSec;
  unsigned long totalMicroSec;
  unsigned long loggedMicroSec;
  static bool firstScan = true;

  totalMicroSec = 0;
  

  //Shift all data one position and get total
  for( i = MAXLOGSIZECYCLETIME-1; i >= 1; i--){
    loggedMicroSec = cycleTimeLog[i-1];  
    totalMicroSec = totalMicroSec + loggedMicroSec;

    cycleTimeLog[i] = loggedMicroSec;
  }

  //Save actual cycle time
  actMicroSec = micros();
  cycleTimeLog[0] = actMicroSec - lastMicroSec;

  lastMicroSec = actMicroSec;

  //Don't do calculations in the first scan.
  if(firstScan == false){
    //Check if actual is more than max
    if(cycleTimeLog[0] > cycleTimeMax)
      cycleTimeMax = cycleTimeLog[0];
  
    //Check if acutal is less than min
    if(cycleTimeLog[0] < cycleTimeMin)
      cycleTimeMin = cycleTimeLog[0];  
  
    //Caculate average
    cycleTimeAvg = (totalMicroSec + cycleTimeLog[0])/MAXLOGSIZECYCLETIME;
    
  }
  else{
    firstScan = false;
  }
  

}



/**************************************************************************************************
* Debug
*
* Select debugmode. When telnet client is connected -> debuginfo to telnet client.
* Else send debuginfo to serialport
**************************************************************************************************/
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

/**************************************************************************************************
* Wifi monitoring
* 
* Monitor number if connection is lost and store last few errors
* If conneciton is lost, try to reconnect every 60 seconds
* if wifi is connected, monitor RSSI
**************************************************************************************************/
void WifiMonitoring(){
  static bool wifiConnected = true;
  unsigned long currentMillis;
  static unsigned long previousMillis = millis();
  static unsigned long startErrorMillis = 0;
  const unsigned long intervalReconnect = 60000;      
  long i;

  //Read current msec
  currentMillis = millis();
  
  if (WiFi.status() != WL_CONNECTED ) { 
    if( wifiConnected == true){
      wifiConnected = false;  
      wifiErrorCount = wifiErrorCount + 1;


      //Shift all data one position
      for( i = MAXLOGSIZEWIFILOG-1; i >= 1; i--){
        LogDataWifiErrors[i] = LogDataWifiErrors[i-1];  
      }

      //Store timestamd start error.
      LogDataWifiErrors[0].dateTime = GetFullFormattedTime();
      startErrorMillis = millis();
      
      
      
    }
          
    //60 second interval for retry reconnect wifi
    if(currentMillis - previousMillis > intervalReconnect) {
      WiFi.begin(ssid, password);
      previousMillis = currentMillis;   
    }
  }
  else {
    //Check if this is the first time connected after no connection
    if(wifiConnected = false){
      //Store duration (seconds)
     LogDataWifiErrors[0].duration = ((millis() - startErrorMillis) / 1000);
    }

    wifiConnected = true;
  
    wifiRSSI = WiFi.RSSI();
  
    if(wifiRSSI > wifiRssiMax)
      wifiRssiMax = wifiRSSI;
  
    if(wifiRSSI < wifiRssiMin)
      wifiRssiMin = wifiRSSI;

    previousMillis = currentMillis;    
  
  }


  if(printWifiMonitoring == 1){
    logger->print("Wifi error count = ");
    logger->println(wifiErrorCount);
    logger->print("Wifi Strength = ");
    logger->print(wifiRSSI);
    logger->print(" , min = ");
    logger->print(wifiRssiMin);
    logger->print(" , max = ");
    logger->println(wifiRssiMax);
    logger->print(" , connected = ");
    logger->println(wifiConnected);
  }
  
}

/**************************************************************************************************
* Telnet
**************************************************************************************************/
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
      TelnetClient.print("Setup last done: ");
      TelnetClient.println(startupDateTime);
      timeClient.update();
      TelnetClient.print("Epoch time: : ");
      TelnetClient.println(timeClient.getEpochTime());
      TelnetClient.print("UTC time: : ");
      TelnetClient.println(GetFullFormattedTime());

      TelnetClient.println("--------------------");
      TelnetClient.print("FreeHeap: : ");
      TelnetClient.println(ESP.getFreeHeap());
      TelnetClient.print("getHeapSize: : ");
      TelnetClient.println(ESP.getHeapSize());
      TelnetClient.print("getMinFreeHeap: : ");
      TelnetClient.println(ESP.getMinFreeHeap());
      TelnetClient.print("FreeHeap: : ");
      TelnetClient.println(ESP.getMaxAllocHeap());
      TelnetClient.println("--------------------");
      TelnetClient.print("wifiErrorCount: : ");
      TelnetClient.println(wifiErrorCount);
      TelnetClient.print("wifiRssiMax: : ");
      TelnetClient.println(wifiRssiMax);
      TelnetClient.print("wifiRssiMin: : ");
      TelnetClient.println(wifiRssiMin);
      TelnetClient.print("wifiRSSI: : ");
      TelnetClient.println(wifiRSSI);


      TelnetClient.println("");
      TelnetClient.println("Wifi error log:");
      TelnetClient.print(LogDataWifiErrors[0].dateTime);
      TelnetClient.print(" : ");
      TelnetClient.println(LogDataWifiErrors[0].duration);
      TelnetClient.print(LogDataWifiErrors[1].dateTime);
      TelnetClient.print(" : ");
      TelnetClient.println(LogDataWifiErrors[1].duration);
      TelnetClient.print(LogDataWifiErrors[2].dateTime);
      TelnetClient.print(" : ");
      TelnetClient.println(LogDataWifiErrors[2].duration);
      TelnetClient.print(LogDataWifiErrors[3].dateTime);
      TelnetClient.print(" : ");
      TelnetClient.println(LogDataWifiErrors[3].duration);

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


/**************************************************************************************************
* SerialP1_Read_Decode
**************************************************************************************************/
void SerialP1_Read_Decode(){

  static unsigned long previousMillisEU; 
  static unsigned long intervalSendEU = 55000;  //Send once a minute, but telegram is send by meter every 10 sec, so allow 5 secends before a minute
  static unsigned long previousMillisGas;
  static unsigned long intervalSendGas = 250000; //Forced update interval for gas.



  if (Serial2.available()) {
    memset(telegram, 0, sizeof(telegram)); //Fill with zero's
    while (Serial2.available()) {
      int len = Serial2.readBytesUntil('\n', telegram, MAXLINELENGTH-2);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();

      //logger->print("Length telegram = ");
      //logger->println(len);
      if(printSerialP1Raw){
        logger->print("Raw P1 telegram = ");
        logger->println(telegram);
      }

      //Decode telegram and check if a new complete telegram is available
      if(decodeTelegram(len+1)){
        currentMillis = millis();
        
        //60 second interval for updating usage Electra and gas.  
        if(currentMillis - previousMillisEU > intervalSendEU) {
            
            previousMillisEU = currentMillis;   

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


/**************************************************************************************************
* SaveDataActual
*
* Save data in a array (que or shift register).
* When new data is available, shift all data one position. 
* Add new data to position 0
* When the complete array is full, send data to SQL server and empty array.
**************************************************************************************************/
void SaveDataActual(){

  long i;

  //Shift all data one position
  for( i = MAXLOGSIZE-1; i >= 1; i--){
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
    for( i = 0; i <= MAXLOGSIZE-1; i++){
      logger->print(i);
      logger->print(" = ");
      logger->println(LogDataActual[i].dateTime);
      logger->print(" ; ");
      logger->println(LogDataActual[i].mEAV);
    }
  }
  
  //Check if array is full
  if( LogDataActual[MAXLOGSIZE-1].dateTime > 0 ) {
    logger->println("Sending data.....");    
    SendMultiElectraActual();

    //Empty array
    for( i = MAXLOGSIZE-1; i >= 0; i--){
      LogDataActual[i] = (udtElectricalActual) {0,0,0,0,0};;  
    }

  }

}


/**************************************************************************************************
* SendMultiElectraActual
**************************************************************************************************/
void SendMultiElectraActual(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHPMulti);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
    // Prepare your HTTP POST request data
    char* httpRequestData;
    httpRequestData = (char *)malloc(sizeof(char) * 1000);

    if(httpRequestData != NULL )
    {
  
    
      strcpy (httpRequestData, "api_key=");
      strcat (httpRequestData, apiKeyValue);
      strcat (httpRequestData, "&sensorId=");
      strcat (httpRequestData, sensorNameActual);

      char dateTimechar[15];
      ltoa(LogDataActual[9].dateTime,dateTimechar,10); 
      char mEAVchar[15];
      ltoa(LogDataActual[9].mEAV,mEAVchar,10); 
      char mEAV1char[15];
      ltoa(LogDataActual[9].mEAV1,mEAV1char,10); 
      char mEAV2char[15];
      ltoa(LogDataActual[9].mEAV2,mEAV2char,10); 
      char mEAV3char[15];
      ltoa(LogDataActual[9].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_0=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_0=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_0=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_0=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_0=");
      strcat (httpRequestData, mEAV3char);
      
      //char dateTimechar[15];
      ltoa(LogDataActual[8].dateTime,dateTimechar,10); 
      //char mEAVchar[15];
      ltoa(LogDataActual[8].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[8].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[8].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[8].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_1=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_1=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_1=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_1=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_1=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[7].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[7].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[7].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[7].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[7].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_2=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_2=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_2=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_2=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_2=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[6].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[6].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[6].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[6].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[6].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_3=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_3=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_3=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_3=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_3=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[5].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[5].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[5].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[5].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[5].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_4=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_4=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_4=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_4=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_4=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[4].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[4].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[4].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[4].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[4].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_5=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_5=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_5=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_5=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_5=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[3].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[3].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[3].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[3].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[3].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_6=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_6=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_6=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_6=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_6=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[2].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[2].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[2].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[2].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[2].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_7=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_7=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_7=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_7=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_7=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[1].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[1].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[1].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[1].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[1].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_8=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_8=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_8=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_8=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_8=");
      strcat (httpRequestData, mEAV3char);     

      //char dateTimechar[15];
      ltoa(LogDataActual[0].dateTime,dateTimechar,10); 
      ////char mEAVchar[15];
      ltoa(LogDataActual[0].mEAV,mEAVchar,10); 
      //char mEAV1char[15];
      ltoa(LogDataActual[0].mEAV1,mEAV1char,10); 
      //char mEAV2char[15];
      ltoa(LogDataActual[0].mEAV2,mEAV2char,10); 
      //char mEAV3char[15];
      ltoa(LogDataActual[0].mEAV3,mEAV3char,10);     
      
      strcat (httpRequestData, "&datetime_9=");
      strcat (httpRequestData, dateTimechar);
      strcat (httpRequestData, "&value1_9=");
      strcat (httpRequestData, mEAVchar);
      strcat (httpRequestData, "&value2_9=");
      strcat (httpRequestData, mEAV1char);
      strcat (httpRequestData, "&value3_9=");
      strcat (httpRequestData, mEAV2char);
      strcat (httpRequestData, "&value4_9=");
      strcat (httpRequestData, mEAV3char);     
      
      
  
     
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

    }
    else{
      logger->println("Failed to allocate memory");
    }

    //Free memory
    free(httpRequestData);

    
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}




/**************************************************************************************************
* SendGasUsage
**************************************************************************************************/
void SendGasUsage(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Prepare your HTTP POST request data
    char* httpRequestData;
    httpRequestData = (char *)malloc(sizeof(char) * 1000);

    if(httpRequestData != NULL )
    {
      char mGASchar[15];
      ltoa(mGAS,mGASchar,10); 
 
      strcpy (httpRequestData, "api_key=");
      strcat (httpRequestData, apiKeyValue);
      strcat (httpRequestData, "&sensorId=");
      strcat (httpRequestData, sensorNameGas);
      strcat (httpRequestData, "&value1=");
      strcat (httpRequestData, mGASchar);
      strcat (httpRequestData, "&value2=");
      strcat (httpRequestData, "0");
      strcat (httpRequestData, "&value3=");
      strcat (httpRequestData, "0");
      strcat (httpRequestData, "&value4=");
      strcat (httpRequestData, "0");
  
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
    }
    else{
      logger->println("Failed to allocate memory");
    }

    //Free memory
    free(httpRequestData);
   
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}


/**************************************************************************************************
* SendWaterUsage
**************************************************************************************************/
void SendWaterUsage(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Prepare your HTTP POST request data
    char* httpRequestData;
    httpRequestData = (char *)malloc(sizeof(char) * 1000);

    if(httpRequestData != NULL )
    {
      char mWAVchar[15];
      ltoa(mWAV,mWAVchar,10); 
 
      strcpy (httpRequestData, "api_key=");
      strcat (httpRequestData, apiKeyValue);
      strcat (httpRequestData, "&sensorId=");
      strcat (httpRequestData, sensorNameWater);
      strcat (httpRequestData, "&value1=");
      strcat (httpRequestData, mWAVchar);
      strcat (httpRequestData, "&value2=");
      strcat (httpRequestData, "0");
      strcat (httpRequestData, "&value3=");
      strcat (httpRequestData, "0");
      strcat (httpRequestData, "&value4=");
      strcat (httpRequestData, "0");
  
      // Send HTTP POST request
      int httpResponseCode = http.POST(httpRequestData);
  
      //Check response code    
      if (httpResponseCode>0) {
        if(printHTTPClient){
          logger->print("HTTP Response code Water Usage: ");
          logger->println(httpResponseCode);
        }
      }
      else {
        logger->print("Error code Water Usage: ");
        logger->println(httpResponseCode);
        logger->println(httpRequestData);
      }
    }
    else{
      logger->println("Failed to allocate memory");
    }

    //Free memory
    free(httpRequestData);
   
    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}





/**************************************************************************************************
* SendElectraUsage
**************************************************************************************************/
void SendElectraUsage(){
  if(WiFi.status()== WL_CONNECTED){

    HTTPClient http; //Define HTTPClient
    
    // Start HTTPClient
    http.begin(serverNamePHP);

    // Add content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    long mEVTT = mEVLT + mEVHT;

    // Prepare your HTTP POST request data
    char* httpRequestData;
    httpRequestData = (char *)malloc(sizeof(char) * 1000);

    if(httpRequestData != NULL )
    {
      char mEVTTchar[15];
      ltoa(mEVTT,mEVTTchar,10); 
      char mEVLTchar[15];
      ltoa(mEVLT,mEVLTchar,10); 
      char mEVHTchar[15];
      ltoa(mEVHT,mEVHTchar,10); 

      strcpy (httpRequestData, "api_key=");
      strcat (httpRequestData, apiKeyValue);
      strcat (httpRequestData, "&sensorId=");
      strcat (httpRequestData, sensorNameUsage);
      strcat (httpRequestData, "&value1=");
      strcat (httpRequestData, mEVTTchar);
      strcat (httpRequestData, "&value2=");
      strcat (httpRequestData, mEVLTchar);
      strcat (httpRequestData, "&value3=");
      strcat (httpRequestData, mEVHTchar);
      strcat (httpRequestData, "&value4=");
      strcat (httpRequestData, "0");   

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
    }
    else{
      logger->println("Failed to allocate memory");
    }

    //Free memory
    free(httpRequestData);
   

    // Free resources
    http.end();
    
  }
  else {
    logger->println("WiFi Disconnected");
  }
}




/**************************************************************************************************
* decodeTelegram
**************************************************************************************************/
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

      //Water is not read by this function, but for monitoring it's easyer to print it here.....
      logger->print("mWAV (Water) is ");
      logger->println(mWAV);  


      logger->println("------------------------------------------");
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




/**************************************************************************************************
* isNumber
**************************************************************************************************/
bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}


/**************************************************************************************************
// FindCharInArrayRev
**************************************************************************************************/
int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }

  return -1;
}





/**************************************************************************************************
* getValue
**************************************************************************************************/
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
