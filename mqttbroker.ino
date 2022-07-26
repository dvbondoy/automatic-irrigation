//RTClib by adafruit
#include "RTClib.h"
#include <ESP8266WiFi.h>
#include "uMQTTBroker.h"

RTC_DS3231 rtc;

char ssid[] = "MQTTBROKER";     // your network SSID (name)
char pass[] = "cGFzc3dvcmQK"; // change this
bool WiFiAP = true;      // Do yo want the ESP as AP?

int SCHED_HOUR;// set/schedule
int SCHED_MINUTE; // set/schedule
//int OFF_HOUR;
//int OFF_MINUTE;
int TEMP_TRIGGER = 28;
int TEMP_TRIGGER_INTERVAL = 5; // set/interval

int WATER_DURATION = 1; // set/duration
bool SCHEDULE_FLAG = false; // set/flag-sched, sched flag
bool TEMP_TRIGGER_FLAG = false; // set/flag-trig, temperature trigger flag
bool WATER_FLAG = false; // flag for solenoid

unsigned long lastMsg = 0;
unsigned long lastTrig = 0;
int counter = 0; //temp trigger interval counter
int auto_off_counter = 0; //water duration counter

DateTime now;

void onConnected();
void pubTempTrigger();
void waterToggle();

/*
   Custom broker class with overwritten callback functions
*/
class myMQTTBroker: public uMQTTBroker
{
  public:
    virtual bool onConnect(IPAddress addr, uint16_t client_count) {
      Serial.println(addr.toString() + " connected");
      onConnected();
      return true;
    }
//
//    virtual void onDisconnect(IPAddress addr, String client_id) {
//      Serial.println(addr.toString() + " (" + client_id + ") disconnected");
//    }
//
//    virtual bool onAuth(String username, String password, String client_id) {
//      Serial.println("Username/Password/ClientId: " + username + "/" + password + "/" + client_id);
//      return true;
//    }

    virtual void onData(String topic, const char *data, uint32_t length) {
      char data_str[length + 1];
      os_memcpy(data_str, data, length);
      data_str[length] = '\0';

      String d = (String)data_str;

      Serial.println("received topic '" + topic + "' with data '" + (String)data_str + "'");

      if(topic == "set/schedule"){
        sscanf(data_str, "%d:%d", &SCHED_HOUR, &SCHED_MINUTE);
      }else if(topic == "set/duration"){
        WATER_DURATION = d.toInt();
      }else if(topic == "set/flag-trig"){
        if(d == "1"){
          TEMP_TRIGGER_FLAG = true;
          counter = 0; //reset counter
          pubTempTrigger();
        }else if(d == "0"){
          TEMP_TRIGGER_FLAG = false;
        }
        Serial.print("temp trigger flag is: ");
        Serial.println(TEMP_TRIGGER_FLAG);
      }else if(topic == "set/temp"){
        TEMP_TRIGGER = d.toInt();
      }else if(topic == "set/flag-sched"){
        d == "1" ? SCHEDULE_FLAG = true : SCHEDULE_FLAG = false;
      }else if(topic == "set/interval"){
        TEMP_TRIGGER_INTERVAL = d.toInt();
      }else if(topic == "water/solenoid-1"){
        if(d.toInt() == 1){
          WATER_FLAG = true;
          auto_off_counter = 0;
        }else{
          WATER_FLAG = false;
        }
        Serial.print("Water flag is: ");
        Serial.println(WATER_FLAG);
      }
    }
};

myMQTTBroker myBroker;

/*
   WiFi init stuff
*/
void setupWiFiAP()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);
  Serial.println("AP started");
  Serial.println("IP address: " + WiFi.softAPIP().toString());
}

void setupRTC()
{
#ifndef ESP8266
  while (!Serial); // wait for serial port to connect. Needed for native USB
#endif

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println();

  setupWiFiAP();

  setupRTC();

  // Start the broker
  Serial.println("Starting MQTT broker");
  myBroker.init();

  myBroker.subscribe("#"); //Subscribe to anything
}

void onConnected(){
  Serial.println("You are connected!");
  myBroker.publish("set/duration",(String)WATER_DURATION);
  myBroker.publish("set/temp",(String)TEMP_TRIGGER);
}

void pubTempTrigger(){
  myBroker.publish("water/solenoid-1", "1");
}

void loop()
{
  if(millis() - lastMsg >= 5000){
    lastMsg = millis();
    now = rtc.now();
    int temp = rtc.getTemperature();
    
    int m = now.minute();
    m += 4; //offset hack
    m > 60 ? m -= 60 : m;

    // auto-off
    if(WATER_FLAG == true){
      auto_off_counter += 5000;
      Serial.print("auto off counter: ");
      Serial.println(auto_off_counter);
      
      if((auto_off_counter / 60000) >= WATER_DURATION){
        auto_off_counter = 0;//reset counter
        myBroker.publish("water/solenoid-1", "0");//turn off solenoid
      }
    }

    // schedule
    if(WATER_FLAG == false && SCHEDULE_FLAG == true){
      Serial.print("sched hour: ");
      Serial.println(SCHED_HOUR);
      Serial.print("sched minute: ");
      Serial.println(SCHED_MINUTE);
      Serial.print("now is: ");
      Serial.println(now.hour());
      Serial.print("m is: ");
      Serial.println(m);

//      Serial.print("aut-water in: ");
//      Serial.print(SCHED_HOUR - now.hour());
//      Serial.print(" hour(s) and ");
//      Serial.print
      if(now.hour() == SCHED_HOUR && m == SCHED_MINUTE){
        myBroker.publish("water/solenoid-1", "1");
      }
    }

    // temperature trigger
    if(TEMP_TRIGGER_FLAG == true && temp >= TEMP_TRIGGER){
      Serial.print("counter: ");
      Serial.println(counter);
      counter += 5000;
      if(WATER_FLAG == false && (counter / 60000) >= TEMP_TRIGGER_INTERVAL){
        counter = 0;//reset counter
        pubTempTrigger();
      }
    }

    myBroker.publish("system/temp", (String)rtc.getTemperature());

    String system_time = (String)now.hour() + ":" + (String)now.minute();
    myBroker.publish("system/time", system_time);
  }

  delay(1000);
}
