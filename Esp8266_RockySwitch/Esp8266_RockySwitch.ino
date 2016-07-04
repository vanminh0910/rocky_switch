#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <IPAddress.h>
#include <PubSubClient.h>

#define SERIAL_BAUD    115200

const char* ssid = "Sandiego";
const char* password = "0988807067";

const char* clientId = "RockySwitch1";
const char* mqttServer = "192.168.1.110";
const char* mqttUsername = "<MQTT_BROKER_USERNAME>";
const char* mqttPassword = "<MQTT_BROKER_PASSWORD>";
const char* relayStatusTopics[] = {
  "/Bedroom1/Light1/Status", 
  "/Bedroom1/Light2/Status",
  "/Bedroom1/Light3/Status"
}; // should change to something unique

const char* relayCmdTopics[] = {
  "/Bedroom1/Light1/Command", 
  "/Bedroom1/Light2/Command",
  "/Bedroom1/Light3/Command"
}; // should change to something unique

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastReconnectTime;
const long reconnectInterval = 30000;

const byte buttonPins[3] = {D0, D1, D2};
const byte relayPins[3]  = {D7, D6, D5};
byte relayStatus[3] = {0, 0, 0};

// used for button debouncing
const byte debounceDelay = 50; // Debounce time;
unsigned long lastDebounceTime = 0; //The last time button was toggled
boolean buttonPressed = false;
byte currentButtonStatus[3];
byte lastButtonStatus[3];

void setup() {
  Serial.begin(SERIAL_BAUD);

  // Configure input pins and output pins
  for(int i=0; i<sizeof(relayPins); i++)
  {
    pinMode(relayPins[i], OUTPUT); // Configure pin2,3,4 as an output 
    pinMode(buttonPins[i], INPUT); // Configure pin5,6,7 as an pull-up input
    digitalWrite(relayPins[i], LOW); //init OFF all relay
    lastButtonStatus[i] = 0;
  }

  setup_wifi();

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("Bedroom1Switch");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  client.setServer(mqttServer, 1883);
  client.setCallback(onMessageReceived);

  // Waiting for connection ready before sending update
  delay(500); 
  sendStatusUpdate();
}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectTime >= reconnectInterval) {
      lastReconnectTime = currentMillis;
      reconnect();
    }
  }

  if (client.connected()) {
    client.loop();
  }

  // Read input pins to check button is clicked or not
  boolean buttonPressed = false;
  for (byte i=0; i<sizeof(buttonPins); i++)
  {
    byte reading = digitalRead(buttonPins[i]); // Read the state of the switch

    // If the switch changed, due to noise or pressing:
    if( reading != lastButtonStatus[i])                   
    {
      lastDebounceTime = millis(); // Reset debouncing timer
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay)      
    {
      // Check button is clicked, change status of relay
      if( reading == HIGH)                 
      { 
        relayStatus[i] = 1 - relayStatus[i];
        digitalWrite(relayPins[i], relayStatus[i]);
        buttonPressed = true;
        //Print out the value of the relay status
        Serial.print("Changed relay "); Serial.print(i); Serial.print(" status to "); Serial.println(relayStatus[i]);
      }
    }
    lastButtonStatus[i] = reading;
  }

  if (buttonPressed) {
    sendStatusUpdate();
    buttonPressed = false;
  }
}

void setup_wifi() {
  // We start by connecting to a WiFi network
  Serial.print("Connecting to "); Serial.println(ssid);

  WiFi.begin(ssid, password);
  //uncomment to use static ip to shorten config time
  //WiFi.config(IPAddress(192,168,1,220), IPAddress(192,168,1,100), IPAddress(255,255,255,0));

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void onMessageReceived(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  for(int i=0; i<sizeof(relayPins); i++) {
    if (strcmp(topic, relayCmdTopics[i]) == 0) {
      Serial.print("Received message for relay "); Serial.println(i);
      if ((char)payload[0] == '1') {
        Serial.println("Command is to turn on");
        digitalWrite(relayPins[i], HIGH);
        relayStatus[i] = HIGH;
      } else if ((char)payload[0] == '0') {
        Serial.println("Command is to turn off");
        digitalWrite(relayPins[i], LOW);
        relayStatus[i] = LOW;
      } else {
        Serial.print("Invalid command received: "); Serial.println(payload[0]);
      }
      break;
    }
  }
  //sendStatusUpdate();
}

void reconnect() {
  Serial.print("Attempting MQTT connection...");
  // Attempt to connect
  if (client.connect(clientId, mqttUsername, mqttPassword)) {
    Serial.println("connected");
    for(int i=0; i<sizeof(relayPins); i++) {
      Serial.print("Subscribe to topic: "); Serial.print(relayCmdTopics[i]);
      client.subscribe(relayCmdTopics[i]);
      Serial.println("... done");
      delay(200);
    }
    Serial.println("Subscribed all topics to broker");
    sendStatusUpdate();
  } else {
    Serial.print("failed, rc="); Serial.println(client.state());
  }
}

/*  when a relay is manually switched by button, this sends all status update
    to server
*/
void sendStatusUpdate()
{  
  for(int i=0; i<sizeof(relayStatus); i++) {
    Serial.println("Sending status update to broker");
    char status [4];
    sprintf (status, "%d", relayStatus[i]);
    client.publish(relayStatusTopics[i], status);
    delay(100);
  }
}
