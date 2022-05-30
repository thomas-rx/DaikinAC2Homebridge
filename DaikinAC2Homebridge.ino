// https://github.com/arachnetech/homebridge-mqttthing/blob/master/docs/Accessories.md#heater-cooler Help me to create accessory on homebridge

#include <FS.h>               //this needs to be first, or it all crashes and burns...
#include <ArduinoJson.h>      //https://github.com/bblanchon/ArduinoJson
#include <ESP8266WiFi.h>      //https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <ESP8266WebServer.h> //https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer
#include <DNSServer.h>        //https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer
#include <WiFiManager.h>      //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>     //https://github.com/knolleary/pubsubclient
#include <DYIRDaikin.h>       //https://github.com/danny-source/Arduino_DY_IRDaikin

#define button_config 5 // Button to reset wifi config
#define pin_led_Wifi 16 // WIFI RED LED -> D0
#define pin_led_IR 4    // IR LED -> D2
#define pin_led_State 2 // AC STATE LED ->  D4

const char *ssid_apmode = "DaikinAC"; // AP Mode SSID
WiFiClient espClient;
PubSubClient client(espClient); // Create a client connection

DYIRDaikin daikinController; // Create a DYIRDaikin object to control the Daikin AC

/*** Default Value ***/
char charServerMQTT[40] = "mqttserver.local";         // MQTT Server IP
char charPortMQTT[6] = "1883";                        // MQTT Port
char charUsernameMQTT[35] = "Username";               // MQTT Username
char charPasswordMQTT[33] = "Password";               // MQTT Password
char charTopicPrefixMQTT[35] = "DaikinAC2Homebridge"; // MQTT Topic Prefix

bool ARCRemote = true;    // If you have a daikin AC that use a BRC controller, please change this to false.
bool useButton = false;   // If you use a button, please change this to true.
bool useWifiLed = false;  // If you use a wifi state led, please change this to true.
bool useStateLed = false; // If you use a state led, please change this to true.

bool ACPower = false; // AC Power | false = OFF, true = ON
bool ACSwing = false; // AC Swing | false = OFF, true = ON
int ACMode = 0;       // AC Mode  | 0 = FAN, 1 = COOL, 2 = DRY, 3 = HEAT
int ACFanSpeed = 0;   // AC Fan   | 1 = LOW, 2 = MEDIUM, 3 = HIGH, 4 = MAX, 5 = AUTO,
int ACTemp = 24;      // AC Temperature (Celsius or Fahrenheit)
bool debug = false;   // If you want to see debug message, please change this to true.

bool shouldSaveConfig = false; // Flag for saving config to FS
bool initialConfig = false;    // Flag for initial configuration

void saveConfigCallback()
{
  shouldSaveConfig = true; // Set flag to true
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n Starting DaikinAC2Homebridge...");

  daikinController.begin(pin_led_IR);

  if (useButton)
    pinMode(button_config, INPUT_PULLUP);
  if (useWifiLed)
    pinMode(pin_led_Wifi, OUTPUT);
  if (useStateLed)
    pinMode(pin_led_State, OUTPUT);

  if (debug)
    SPIFFS.format(); // For debug only

  Serial.println("Mounting FS...");

  if (SPIFFS.begin())
  {
    Serial.println("File system mounted.");

    if (SPIFFS.exists("/config.json"))
    {
      Serial.println("Reading config file...");

      File configFile = SPIFFS.open("/config.json", "r");

      if (configFile)
      {
        Serial.println("Config file opened.");

        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]); // create buffer to store contents of file

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonBuffer(1024);

        DeserializationError error = deserializeJson(jsonBuffer, buf.get());

        if (error)
        {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }

        Serial.println("\nJSON has been loaded, parsing now...");

        strcpy(charServerMQTT, jsonBuffer["charServerMQTT"]);
        strcpy(charPortMQTT, jsonBuffer["charPortMQTT"]);
        strcpy(charUsernameMQTT, jsonBuffer["charUsernameMQTT"]);
        strcpy(charPasswordMQTT, jsonBuffer["charPasswordMQTT"]);
        strcpy(charTopicPrefixMQTT, jsonBuffer["charTopicPrefixMQTT"]);
      }
    }
  }
  else
  {
    Serial.println("Error: File system not mounted.");
  }

  Serial.println("\nChecking WiFi...");
  Serial.println(WiFi.SSID());

  if (WiFi.SSID() == "") // Connection verification
  {
    Serial.println("WiFi is not connected or not configured.");
    initialConfig = true;
  }

  else
  {
    Serial.println("WiFi seems to be configured.");
  }

  ARCRemote ? client.setCallback(callbackARC) : client.setCallback(callbackBRC);
}

void createWifi()
{
  WiFiManagerParameter custom_server_MQTT("charServerMQTT", "server MQTT", charServerMQTT, 40, "title= \"Parameter 1: Server or Host for MQTT example: 192.168.0.12 or domain.com\"");
  WiFiManagerParameter custom_port_MQTT("charPortMQTT", "port MQTT", charPortMQTT, 6, "title= \"Parameter 2: Port 1883 default. \"   type=\"number\"  ,  min=\"1\" max=\"65535\"   ");
  WiFiManagerParameter custom_username_MQTT("charUsernameMQTT", "username MQTT", charUsernameMQTT, 36, "title= \"Parameter 3: Username for MQTT. \"   maxlength=\"35\"  \"");
  WiFiManagerParameter custom_password_MQTT("charPasswordMQTT", "password MQTT", charPasswordMQTT, 34, "title= \"Parameter 4: Password for MQTT. \"   maxlength=\"33\"  \"");
  WiFiManagerParameter custom_topic_prefix_MQTT("charTopicPrefixMQTT", "topic", charTopicPrefixMQTT, 36, "title= \"Parameter 5: Topic prefix for MQTT. \"   maxlength=\"35\"  \"");

  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_text("<p><strong>MQTT Configuration</strong></p>");
  wifiManager.addParameter(&custom_text);
  wifiManager.addParameter(&custom_server_MQTT);
  wifiManager.addParameter(&custom_port_MQTT);
  wifiManager.addParameter(&custom_topic_prefix_MQTT);
  wifiManager.addParameter(&custom_username_MQTT);
  wifiManager.addParameter(&custom_password_MQTT);

  String ssid = String(ssid_apmode) + String("-") + String(ESP.getChipId());

  if (!wifiManager.startConfigPortal(ssid.c_str()))
  {
    Serial.println("Failed to connect to WiFi and hit timeout.");
    delay(3000);

    ESP.reset();
    delay(5000);
  }

  strcpy(charServerMQTT, custom_server_MQTT.getValue());
  strcpy(charPortMQTT, custom_port_MQTT.getValue());
  strcpy(charUsernameMQTT, custom_username_MQTT.getValue());
  strcpy(charPasswordMQTT, custom_password_MQTT.getValue());
  strcpy(charTopicPrefixMQTT, custom_topic_prefix_MQTT.getValue());

  if (shouldSaveConfig)
  {
    Serial.println("saving config");
    DynamicJsonDocument jsonBuffer(1024);
    DynamicJsonDocument json(1024);

    Serial.print("charServerMQTT: ");
    Serial.println(charServerMQTT);

    Serial.print("charPortMQTT: ");
    Serial.println(charPortMQTT);

    Serial.print("charUsernameMQTT: ");
    Serial.println(charUsernameMQTT);

    Serial.print("charPasswordMQTT: ");
    Serial.println(charPasswordMQTT);

    Serial.print("charServerMQTT: ");
    Serial.println(charTopicPrefixMQTT);

    json["charServerMQTT"] = charServerMQTT;
    json["charPortMQTT"] = charPortMQTT;
    json["charUsernameMQTT"] = charUsernameMQTT;
    json["charPasswordMQTT"] = charPasswordMQTT;
    json["charTopicPrefixMQTT"] = charTopicPrefixMQTT;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
      Serial.println("Failed to open config file for writing.");

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
  }
}

/* MQTT STOCK START */

void publishMQTTMessage(char *topic, char *payload, boolean retain)
{
  client.publish(topic, (uint8_t *)payload, strlen(payload), retain);
}

void publishMQTTMessage(char *topic, boolean payload, boolean retain)
{
  publishMQTTMessage(topic, (payload ? (char *)"True" : (char *)"False"), retain);
}

void publishMQTTMessage(char *topic, uint8_t payload, boolean retain)
{
  String str = String(payload);
  char buf[sizeof(str)];
  str.toCharArray(buf, sizeof(buf));
  publishMQTTMessage(topic, buf, retain);
}

void publishMQTTMessage(char *topic, unsigned int payload, boolean retain)
{
  String str = String(payload);
  char buf[sizeof(str)];
  str.toCharArray(buf, sizeof(buf));
  publishMQTTMessage(topic, buf, retain);
}

void publishMQTTMessage(char *topic, String payload, boolean retain)
{
  char charArray[sizeof(payload)];
  payload.toCharArray(charArray, sizeof(charArray));
  publishMQTTMessage(topic, charArray, true);
}

void reconnect() // Loop until we're reconnected
{
  if (!client.connected() && WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Attempting MQTT connection...");

    char charDeviceLabel[50];
    String deviceLabel = String(ssid_apmode) + String("_") + String(ESP.getChipId());
    deviceLabel.toCharArray(charDeviceLabel, 50);

    if (debug)
    {
      Serial.print("MQTT Client ID: ");
      Serial.println(charDeviceLabel);

      Serial.print("MQTT Username: ");
      Serial.println(charUsernameMQTT);

      Serial.print("MQTT Password: ");
      Serial.println(charPasswordMQTT);
    }

    client.setServer(charServerMQTT, String(charPortMQTT).toInt());

    if (client.connect(charDeviceLabel, charUsernameMQTT, charPasswordMQTT))
    {
      Serial.println("Connected to MQTT server");
      subscribeToTopic("%s/Power");
      subscribeToTopic("%s/Mode");
      subscribeToTopic("%s/Temperature");
      subscribeToTopic("%s/FanSpeed");
      subscribeToTopic("%s/Swing");
    }

    else
    {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
    }
  }
}

void subscribeToTopic(char *topic)
{
  char completeTopic[50];
  sprintf(completeTopic, topic, charTopicPrefixMQTT);
  client.subscribe(completeTopic);
  Serial.print("Subscribe to:");
  Serial.println(completeTopic);
}

void callbackARC(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  String actualTopic = topic;

  actualTopic = actualTopic.substring(strlen(charTopicPrefixMQTT) + 1);
  Serial.print(actualTopic);
  Serial.print("] ");

  payload[length] = '\0';

  String actualPayload = String((char *)payload);
  Serial.println(actualPayload);

  if (actualTopic.equals("Power"))
  {
    if (actualPayload.equals("true"))
    {
      ACPower = true;
      useStateLed ? digitalWrite(pin_led_State, HIGH) : digitalWrite(pin_led_State, LOW);
    }

    else if (actualPayload.equals("false"))
    {
      ACPower = false;
      digitalWrite(pin_led_State, LOW);
    }

    if (debug)
    {
      Serial.print("SETTING POWER TO: ");
      Serial.println(actualPayload);
    }
  }

  else if (actualTopic.equals("Mode"))
  {
    if (actualPayload.equals("false"))
    {
      ACPower = false;
      digitalWrite(pin_led_State, LOW);
    }

    else
    {
      ACPower = true;
      String modes[] = {"FAN", "COOL", "DRY", "HEAT"};

      for (int i = 0; i < sizeof(modes); i++)
      {
        if (actualPayload.equals(modes[i]))
        {
          ACMode = i;
          useStateLed ? digitalWrite(pin_led_State, HIGH) : digitalWrite(pin_led_State, LOW);
          break;
        }
      }
    }

    if (debug)
    {
      Serial.print("SETTING MODE TO: ");
      Serial.println(actualPayload);
    }
  }

  else if (actualTopic.equals("Temperature"))
  {
    ACTemp = actualPayload.toInt();

    if (debug)
    {
      Serial.print("SETTING TEMPERATURE TO: ");
      Serial.println(actualPayload.toInt());
    }
  }

  else if (actualTopic.equals("FanSpeed"))
  {
    int fanSpeed = actualPayload.toInt();

    if (fanSpeed > 0 && fanSpeed < 25)
    {
      ACFanSpeed = 1;
    }

    else if (fanSpeed >= 25 && fanSpeed < 50)
    {
      ACFanSpeed = 2;
    }

    else if (fanSpeed >= 50 && fanSpeed < 75)
    {
      ACFanSpeed = 3;
    }

    else if (fanSpeed >= 75 && fanSpeed < 100)
    {
      ACFanSpeed = 4;
    }

    else if (fanSpeed == 100)
    {
      ACFanSpeed = 5;
    }

    if (debug)
    {
      Serial.print("SETTING FAN SPEED TO: ");
      Serial.println(ACFanSpeed);
    }
  }

  else if (actualTopic.equals("Swing"))
  {

    if (actualPayload.equals("ENABLED"))
    {
      ACSwing = true;
    }

    else if (actualPayload.equals("DISABLED"))
    {
      ACSwing = false;
    }

    if (debug)
    {
      Serial.print("SETTING SWING TO: ");
      Serial.println(actualPayload);
    }
  }

  Serial.println("Sending this to the AC: ");
  Serial.println("-------------------------------------");
  Serial.print("ACPower: ");
  Serial.println(ACPower);
  Serial.print("ACMode: ");
  Serial.println(ACMode);
  Serial.print("ACTemp: ");
  Serial.println(ACTemp);
  Serial.print("ACFanSpeed: ");
  Serial.println(ACFanSpeed);
  Serial.print("ACSwing: ");
  Serial.println(ACSwing);
  Serial.println("-------------------------------------");

  ACPower ? daikinController.on() : daikinController.off();
  ACSwing ? daikinController.setSwing_on() : daikinController.setSwing_off();
  daikinController.setMode(ACMode);
  daikinController.setTemp(ACTemp);
  daikinController.setFan(ACFanSpeed);
  daikinController.sendCommand();
}

void callbackBRC(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  String actualTopic = topic;

  actualTopic = actualTopic.substring(strlen(charTopicPrefixMQTT) + 1);

  Serial.print(actualTopic);
  Serial.print("] ");

  payload[length] = '\0';

  String actualPayload = String((char *)payload);
  Serial.println(actualPayload);

  if (actualTopic.equals("Power"))
  {
    if (actualPayload.equals("true"))
    {
      ACPower = true;
      useStateLed ? digitalWrite(pin_led_State, HIGH) : digitalWrite(pin_led_State, LOW);
    }

    else if (actualPayload.equals("false"))
    {
      ACPower = false;
      digitalWrite(pin_led_State, LOW);
    }

    if (debug)
    {
      Serial.print("SETTING POWER TO: ");
      Serial.println(actualPayload);
    }
  }

  else if (actualTopic.equals("Temperature"))
  {
    ACTemp = actualPayload.toInt();

    if (debug)
    {
      Serial.print("SETTING TEMPERATURE TO: ");
      Serial.println(actualPayload.toInt());
    }
  }

  else if (actualTopic.equals("FanSpeed"))
  {
    int fanSpeed = actualPayload.toInt();

    if (fanSpeed > 0 && fanSpeed < 25)
    {
      ACFanSpeed = 1;
    }

    else if (fanSpeed >= 25 && fanSpeed < 50)
    {
      ACFanSpeed = 2;
    }

    else if (fanSpeed >= 50 && fanSpeed < 75)
    {
      ACFanSpeed = 3;
    }

    else if (fanSpeed >= 75 && fanSpeed < 100)
    {
      ACFanSpeed = 4;
    }

    else if (fanSpeed == 100)
    {
      ACFanSpeed = 5;
    }

    if (debug)
    {
      Serial.print("SETTING FAN SPEED TO: ");
      Serial.println(ACFanSpeed);
    }
  }

  else if (actualTopic.equals("Swing"))
  {

    if (actualPayload.equals("ENABLED"))
    {
      ACSwing = true;
    }

    else if (actualPayload.equals("DISABLED"))
    {
      ACSwing = false;
    }

    if (debug)
    {
      Serial.print("SETTING SWING TO: ");
      Serial.println(actualPayload);
    }
  }

  Serial.println("Sending this to the AC: ");
  Serial.println("-------------------------------------");
  Serial.print("ACPower: ");
  Serial.println(ACPower);
  Serial.print("ACTemp: ");
  Serial.println(ACTemp);
  Serial.print("ACFanSpeed: ");
  Serial.println(ACFanSpeed);
  Serial.print("ACSwing: ");
  Serial.println(ACSwing);
  Serial.println("-------------------------------------");

  ACPower ? daikinController.on() : daikinController.off();
  ACSwing ? daikinController.setSwing_on() : daikinController.setSwing_off();
  daikinController.setTemp(ACTemp);
  daikinController.setFan(ACFanSpeed);
  daikinController.sendCommand();
}

/* MQTT STOCK END */

void loop()
{

  if (initialConfig)
  {
    Serial.println("Network connection failed.");
    Serial.println("configure network credentials (Config button)");
    delay(2000);
  }

  // MQTT connection
  if (!client.connected())
  {
    reconnect();
    delay(2000);
  }

  else
  {
    client.loop();
  } // MQTT loop

  if (WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(pin_led_Wifi, LOW);
  }

  else
  {
    useWifiLed ? digitalWrite(pin_led_Wifi, HIGH) : digitalWrite(pin_led_Wifi, LOW);
  }

  if (useButton && digitalRead(button_config) == HIGH)
  {
    digitalWrite(pin_led_Wifi, LOW);
    Serial.println("Creating a Wifi to configure the ESP...");
    createWifi();
  }
}
