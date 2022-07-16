  #include <Arduino.h>

// used to get the reset reason
  extern "C" {
  #include <user_interface.h>
  }
  int rst_cause;

// Stuff for the WiFi manager 
  #include "LittleFS.h"
  #include "WiFiManager.h"
  #include "webServer.h"
  #include "updater.h"
  #include "fetch.h"
  #include "configManager.h"
  #include "dashboard.h"
  #include "timeSync.h"
  #include <TZ.h>
  #include "ESP8266HTTPClient.h"
  #include "ESP8266mDNS.h"

// Stuff for MODBUS
  #include <SensorModbusMaster.h>
  // Define the sensor's modbus address
  byte modbusAddress = 0x01;   // The sensor's modbus address, or SlaveID
  long modbusBaudRate = 9600; // The baud rate the sensor uses
  // Construct the modbus instance
  modbusMaster modbus;
  /*
    Since this is using the only serial connection available on the esp8266
    all serial printouts (debug prints...) are silenced in this source code.
    This device is not being connected to a computer to read out the serial communications.
  */

// LEDs
  #define Heartbeat_LED D4 // D4 = GPIO2 = onchip LED
  #define Splunking_LED D8 // output pin for measurement and splunking indicator

// variable for timing
  static unsigned long msTickSplunk = 0;

// error counter for http connection -> reset esp after to many errors
  static int httpError = 0;
  

// Software restart 
void(* resetFunc) (void) = 0; //declare reset function @ address 0


/* Returns a string of Localtime
 * in format "%Y-%m-%d %H:%M:%S"
 */
String getLocaltime(){
  time_t now = time(nullptr);
  struct tm * timeinfo;
  char timeStringBuff[20];
    
  time (&now);
  timeinfo = localtime(&now);
    
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", timeinfo);
  return timeStringBuff;
}

/* Returns a string of time in UTC
 * in format "%Y-%m-%d %H:%M:%S"
 */
String getUTC(){
  time_t now = time(nullptr);
  struct tm * timeinfo;
  char timeStringBuff[20];
    
  time (&now);
  timeinfo = gmtime(&now);
    
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", timeinfo);
  return timeStringBuff;
}

/* Returns epoch time as long
 */
long getEpoch(){
  time_t now = time(nullptr);
  return now;
}


/* Post event data to splunk http event collector
 * 
 * PostData: 
 * a string of json formatted key:value pairs 
 * just everything between the curly braces of the event node
 */
void splunkpost(String PostData){

  // only send time string to splunk if it is synced, else omit it and let splunk use index time...
  String timeField = timeSync.isSynced() ? "\"time\" : \"" + String(getEpoch()) + "\" , " : "" ;

  String payload  = "{ \"host\": \"" + String(configManager.data.clientName) + "\", " 
                      "\"sourcetype\": \"" + String(configManager.data.sourcetype) + "\", " 
                      "\"index\": \"" + String(configManager.data.index) + "\", " 
                      + timeField +
                      "\"fields\" : {"
                                      "\"IP\" : \"" + String(WiFi.localIP().toString()) + "\" , "
                                      "\"UTC\" : \"" + String(getUTC()) + "\" , "
                                      "\"Localtime\" : \"" + String(getLocaltime()) + "\" , "
                                      "\"interval\" : \"" + String(configManager.data.updateSpeed/1000) + "\" "
                        "}, "
                        "\"event\"  : {" + PostData + "}"
                    "}";

  //Build the request
  WiFiClient client; // just to avoid deprecation error on http.begin(url)
  HTTPClient http;
  String splunkurl="http://"+ String(configManager.data.splunkindexer) +"/services/collector";
  String tokenValue="Splunk " + String(configManager.data.collectorToken);
  
  // fire at will!! 
  http.begin(client, splunkurl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", tokenValue);

  if (!configManager.data.silenceSerial){Serial.print("splunking: "); Serial.print(payload);}


  String contentlength = String(payload.length());
  http.addHeader("Content-Length", contentlength );
  int httpResponseCode = http.POST(payload);
  if (!configManager.data.silenceSerial){
    http.writeToStream(&Serial);
    Serial.printf("HTTP: %d", httpResponseCode);  
    Serial.println();
  }
  http.end();

  // check for http error
  if (httpResponseCode > 200){
    httpError++;
    if (httpError>10){resetFunc();}
  }

  // send data to dashboard
  dash.data.httpResponse = httpResponseCode;
  dash.send();
}


/* Post metric data to splunk http event collector
 * 
 * PostData: 
 * a string of json formatted key:value pairs 
 * just everything between the curly braces of the event node
 */
void splunkpostMetric(String PostData){

  // only send time string to splunk if it is synced, else omit it and let splunk use index time...
  String timeField = timeSync.isSynced() ? "\"time\" : \"" + String(getEpoch()) + "\" , " : "" ;

  String payload  = "{ \"host\": \"" + String(configManager.data.clientName) + "\", " 
                      "\"sourcetype\": \"" + String(configManager.data.sourcetype) + "\", " 
                      "\"index\": \"" + String("homemetrics") + "\", " 
                      + timeField +
                      "\"fields\" : {"
                                      "\"IP\" : \"" + String(WiFi.localIP().toString()) + "\" , "
                                      "\"interval\" : \"" + String(configManager.data.updateSpeed/1000) + "\" , "
                                      + PostData +
                        "} "
                    "}";

  //Build the request
  WiFiClient client; // just to avoid deprecation error on http.begin(url)
  HTTPClient http;
  String splunkurl="http://"+ String(configManager.data.splunkindexer) +"/services/collector";
  String tokenValue="Splunk " + String(configManager.data.collectorToken);
  
  // fire at will!! 
  http.begin(client, splunkurl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", tokenValue);

  if (!configManager.data.silenceSerial){Serial.print("splunking: "); Serial.print(payload);}


  String contentlength = String(payload.length());
  http.addHeader("Content-Length", contentlength );
  int httpResponseCode = http.POST(payload);
  if (!configManager.data.silenceSerial){
    http.writeToStream(&Serial);
    Serial.printf("HTTP: %d", httpResponseCode);  
    Serial.println();
  }
  http.end();

  // check for http error
  if (httpResponseCode > 200){
    httpError++;
    if (httpError>10){resetFunc();}
  }

}

/* Triggers a software restart
 * resets the flag in config manager
 * send INFO msg to splunk
 */
void forceRestart(){
  // save false to config data.. else it would keep restarting...
  configManager.data.forceRestart = false;
  configManager.save();
  splunkpost("\"status\" : \"INFO\", \"msg\" : \"Executing Order 66\""); 
  resetFunc();
}


/* callback function for configManager save to EEPROM
 * 
 */
void saveCallback(){
  // do stuff on save

  // set new hostname for MDNS on save
  // didnt find a way to get the change within the loop.. so change it here anyways
  MDNS.setHostname(configManager.data.clientName);
}


void setup()
{
  // enable light sleep = wifi/cpu off during delay calls
  //wifi_set_sleep_type(LIGHT_SLEEP_T);

  rst_info *resetInfo;
  resetInfo = ESP.getResetInfoPtr();
  String rst_string = ESP.getResetReason(); // good for logging to splunk as plain text
  rst_cause = resetInfo->reason;        // good to use in code 
  switch (rst_cause){
    case REASON_DEFAULT_RST:      /* normal startup by power on */
      break;

    case REASON_WDT_RST:          /* hardware watch dog reset */
      break;

    case REASON_EXCEPTION_RST:    /* exception reset, GPIO status won’t change */
      break;

    case REASON_SOFT_WDT_RST:     /* software watch dog reset, GPIO status won’t change */
      break;

    case REASON_SOFT_RESTART:     /* software restart ,system_restart , GPIO status won’t change */
      break;

    case REASON_DEEP_SLEEP_AWAKE: /* wake up from deep-sleep */
      break;

    case REASON_EXT_SYS_RST:      /* external system reset */
      break;

    default:
      break;     
  }

  Serial.begin(modbusBaudRate);
  if (!configManager.data.silenceSerial){Serial.println(rst_string);}

  // MODBUS
  // Start the modbus instance
  modbus.begin(modbusAddress, &Serial);



  LittleFS.begin(); GUI.begin(); configManager.begin(); dash.begin(1000);
  configManager.setConfigSaveCallback(saveCallback); // executed everytime a new config is saved

  // get the mac address as int -> hex -> use it as part of wifi name
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  //Serial.println(macAddr[5], HEX);Serial.println(macAddr[4], HEX);Serial.println(macAddr[3], HEX);Serial.println(macAddr[2], HEX);Serial.println(macAddr[1], HEX);Serial.println(macAddr[0], HEX);
  String macHex = String(macAddr[3], HEX) + String(macAddr[4], HEX) + String(macAddr[5], HEX);
  macHex.toUpperCase();

  // add the MAC to the Project name to avoid duplicate SSIDs
  char AP_Name[60]; // 60 should be more than enough...
  strcpy(AP_Name, configManager.data.projectName);
  strcat(AP_Name, " _ ");
  strcat(AP_Name, WiFi.macAddress().c_str());
  WiFiManager.begin(AP_Name); //192.168.4.1

  // if no client name is set (default=MAC_ADRESS) return the MAC instead
  if (String(configManager.data.clientName) == "MAC_ADDRESS"){
    WiFi.macAddress().toCharArray(configManager.data.clientName, 20);
    configManager.save();
  }

  // just for testing.. needs some fixfix
  MDNS.begin(configManager.data.clientName);

  // set mac to dashboard
  WiFi.macAddress().toCharArray(dash.data.macAddress, 20);

  // set the timezone
  timeSync.begin(configManager.data.sensorTimezone);

  // wait for connection
  timeSync.waitForSyncResult(5000);

  // set input/output pins
  pinMode(Heartbeat_LED, OUTPUT); // heartbeat LED
  digitalWrite(Heartbeat_LED, HIGH);
  pinMode(Splunking_LED, OUTPUT); // sending to splunk LED
  digitalWrite(Splunking_LED, LOW);
 
  // only report sensor status on hardware restart.. dont report it after deep-sleep wake
  // after deep-sleep wake silently go into measurement and send data only
  if (rst_cause != REASON_DEEP_SLEEP_AWAKE){
    String msgSensorStatus =  "\"msg\":\"restarted\", "
                              "\"rstReason\":\""  + String(rst_cause) + " - "  + String(rst_string) + "\", "
                              "\"TimeSync\":\""   + String(timeSync.isSynced()) + "\"";

    splunkpost(msgSensorStatus); 
  }
}

bool updateCaptivePortalEnterTimer = true;
long CaptivePortalEnterTimer = 0;

void loop()
{
  // software interrupts.. dont touch next line!
  WiFiManager.loop();updater.loop();configManager.loop();dash.loop();MDNS.update();
  yield();

  if (WiFiManager.isCaptivePortal()){// && rst_cause != REASON_DEEP_SLEEP_AWAKE){
    digitalWrite(Heartbeat_LED, (millis() / 100) % 2);
    
    // suicide after to much time spent in captive portal
    /* if (updateCaptivePortalEnterTimer){
      CaptivePortalEnterTimer=millis();
      updateCaptivePortalEnterTimer = false;
    } 
    
    if (millis()-CaptivePortalEnterTimer > 60000){forceRestart();}
    */
    return;
  }
  else{
    //updateCaptivePortalEnterTimer = true;
    // toggle LED every second if heartbeat is activated in config
    digitalWrite(Heartbeat_LED, configManager.data.heartbeat ? ((millis() / 1000) % 2) : 1);  
  }

  // restart over web interface...
  if (configManager.data.forceRestart){forceRestart();}


  if (millis() - msTickSplunk > configManager.data.updateSpeed){ // || (rst_cause == REASON_DEEP_SLEEP_AWAKE && configManager.data.deepsleep > 0)){
    msTickSplunk = millis();

    //digitalWrite(Splunking_LED, HIGH);
    Serial.println(millis());
  
  // read modbus input register 
  float volt = modbus.float32FromRegister(0x04, 0x00, bigEndian); 
  float ampere =  modbus.float32FromRegister(0x04, 0x06, bigEndian); 
  float watt = modbus.float32FromRegister(0x04, 0x0C, bigEndian);
  //float apparent_power = modbus.float32FromRegister(0x04, 0x12, bigEndian); 
  //float reactive_power = modbus.float32FromRegister(0x04, 0x18, bigEndian); 
  float power_factor = modbus.float32FromRegister(0x04, 0x1E, bigEndian);
  float phase_angle = modbus.float32FromRegister(0x04, 0x24, bigEndian);
  float frequency = modbus.float32FromRegister(0x04, 0x46, bigEndian);
  float energy_imp_active = modbus.float32FromRegister(0x04, 0x48, bigEndian);
  //float energy_imp_reactive = modbus.float32FromRegister(0x04, 0x4C, bigEndian);
  //float power_demand = modbus.float32FromRegister(0x04, 0x0054, bigEndian);
  //float current_power_demand = modbus.float32FromRegister(0x04, 0x0056, bigEndian);
  //float current_demand = modbus.float32FromRegister(0x04, 0x0102, bigEndian);

  //float energy_total_active = modbus.float32FromRegister(0x04, 0x0156, bigEndian);
  //float energy_total_reactive = modbus.float32FromRegister(0x04, 0x0158, bigEndian);

  // build the event data string
    String uptime = (rst_cause == REASON_DEEP_SLEEP_AWAKE) ? "" : ", \"uptime\": \"" + String(millis()/1000) + "\" "; // because deepsleep resets the millis counter --> after wake up it's always zero
    String eventData =  "\"volt\": \"" + String(volt, 3) + "\" "
                      + ", \"ampere\": \"" + String(ampere, 3) + "\" "
                      + ", \"watt\": \"" + String(watt, 3) + "\" "
                      //+ ", \"apparentPower\": \"" + String(apparent_power, 3) + "\" "
                     // + ", \"reactivePower\": \"" + String(reactive_power, 3) + "\" "
                      + ", \"powerFactor\": \"" + String(power_factor, 3) + "\" "
                      + ", \"phaseAngle\": \"" + String(phase_angle, 3) + "\" "
                      + ", \"frequency\": \"" + String(frequency, 3) + "\" "
                      + ", \"energy\": \"" + String(energy_imp_active, 3) + "\" "
                     // + ", \"reactiveEnergy\": \"" + String(energy_imp_reactive, 3) + "\" "
                     // + ", \"currentDemand\": \"" + String(current_demand, 3) + "\" "
                     // + ", \"powerDemand\": \"" + String(power_demand, 3) + "\" "
                      + uptime;

  // build the metric data string
  String uptimeMetric = (rst_cause == REASON_DEEP_SLEEP_AWAKE) ? "" : ", \"metric_name:selfPV.uptime\":" + String(millis()/1000); // because deepsleep resets the millis counter --> after wake up it's always zero
  String metricData =     "\"metric_name:selfPV.current.active\":" + String(ampere, 3)
                    //  + ", \"metric_name:selfPV.current.demand\":" + String(current_demand, 3)
                      + ", \"metric_name:selfPV.energy.import.active\":" + String(energy_imp_active, 3)
                    //  + ", \"metric_name:selfPV.energy.import.reactive\":" + String(energy_imp_reactive, 3)
                    //  + ", \"metric_name:selfPV.energy.total.active\":" + String(energy_total_active, 3)
                    //  + ", \"metric_name:selfPV.energy.total.reactive\":" + String(energy_total_reactive, 3)
                      + ", \"metric_name:selfPV.voltage\":" + String(volt, 3)
                    //  + ", \"metric_name:selfPV.power.demand\":" + String(power_demand, 3)
                    //  + ", \"metric_name:selfPV.power.demand.current\":" + String(current_power_demand, 3)
                      + ", \"metric_name:selfPV.power.active\":" + String(watt, 3)
                    //  + ", \"metric_name:selfPV.power.apparent\":" + String(apparent_power, 3)
                    //  + ", \"metric_name:selfPV.power.reactive\":" + String(reactive_power, 3)
                      + ", \"metric_name:selfPV.power.factor\":" + String(power_factor, 3)
                      + ", \"metric_name:selfPV.phaseAngle\":" + String(phase_angle, 3)
                      + ", \"metric_name:selfPV.frequency\":" + String(frequency, 3)
                      + uptimeMetric;

  //send off the data
    splunkpost(eventData);
    splunkpostMetric(metricData);

  //send single metrics along with units of measurement
  splunkpostMetric("\"location\":\"outdoor\", \"unit\":\"A\", \"metric_name:selfPV1.current.active\":" + String(ampere, 3));
  //splunkpostMetric("\"location\":\"outdoor\", \"unit\":\"A\", \"metric_name:selfPV1.current.demand\":" + String(current_demand, 3));
  splunkpostMetric("\"location\":\"outdoor\", \"unit\":\"kWh\", \"metric_name:selfPV1.energy.import.active\":" + String(energy_imp_active, 3));
  //splunkpostMetric("\"location\":\"outdoor\", \"unit\":\"kVArh\", \"metric_name:selfPV1.energy.import.reactive\":" + String(energy_imp_reactive, 3));
  splunkpostMetric("\"location\":\"outdoor\", \"unit\":\"W\", \"metric_name:selfPV1.power.active\":" + String(watt, 3));

    //digitalWrite(Splunking_LED, LOW);
 
  }

  // sleep a little in light sleep during a delay call.. hopefully reducing heat from the esp
  // fixfix maybe set it to the update intervall.. and just sleep between two updates
  // fixfix bad idea. web gui gets inresponsive at long sleep times
  //delay(configManager.data.delay);

  // stay awake for 60s after hard reset to flash or change config
  // deepsleep config of ZERO will disable deepsleep 
  /*  if (configManager.data.deepsleep != 0){
    if (millis() > 60000 && rst_cause != REASON_DEEP_SLEEP_AWAKE){
        if (!configManager.data.silenceSerial){Serial.println("60s after restart.. going to deepsleep");}
      
      
      ESP.deepSleep(configManager.data.deepsleep * 1000000);
    }
    // if woken up from deep sleep .. go back to deep sleep
    if (rst_cause == REASON_DEEP_SLEEP_AWAKE){
      if (!configManager.data.silenceSerial){Serial.println("woke up, did work, going to sleep again");}
      
      ESP.deepSleep(configManager.data.deepsleep * 1000000);
    } // from seconds in webconfig to us in function call = "* 1000000"
  } */
}