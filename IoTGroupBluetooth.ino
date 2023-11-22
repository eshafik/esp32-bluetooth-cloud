//#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoBLE.h>
#include <ArduinoJson.h>

const char* ubidotsToken = "xxxxx";
const char* variableId = "light-controller"; // Variable Label in Ubidots

const char* mqttServer = "industrial.api.ubidots.com";
const int mqttPort = 1883;

const int ledPin = 2; // D2 on the ESP32

WiFiClient espClient;
PubSubClient client(espClient);

BLEService ledService("19B10000-E8F2-537E-4F6C-D104768A1214"); // BLE LED Service
BLEByteCharacteristic ledCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite); // BLE LED Characteristic

int ledState = 0; // Initial LED state

void setLedState();

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert payload to a string for easier processing
  String payloadStr = String((char*)payload);
  Serial.print("payload: ");
  Serial.println(payloadStr);

  // Check if the message is for the "light-controller" variable
  if (strcmp(topic, "/v1.6/devices/home-automation-group-one/light-controller") == 0) {
    // Parse the payload (assuming it's in JSON format)
    // You might need to use a JSON library for a more robust implementation
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payloadStr);

    // Extract the LED state from the JSON payload
    int ubidotsLedState = doc["value"].as<int>();

    Serial.print("cloud value: ");
    Serial.println(ubidotsLedState);
      // Update the local LED state
      if (ubidotsLedState == 1) {
        ledState = 1;
      } else {
        ledState = 0;
      }

      // Update the physical LED state
      setLedState();
      Serial.println("Received Ubidots command. LED state: " + String(ledState));
    }
  else {
    // If the message is not for "light-controller", ignore it
    Serial.println("Ignoring message for a different variable.");
  }
}


void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, true);

  // Create an instance of WiFiManager
  WiFiManager wifiManager;

  // Uncomment the line below to reset saved settings (for testing purposes)
  // wifiManager.resetSettings();

  // Try to connect to Wi-Fi with saved credentials
  if (!wifiManager.autoConnect("ESP32-AP")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    // Reset and try again, or put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  Serial.println("Connected to Wi-Fi");

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  BLE.setLocalName("ESP32_LED");
  BLE.setAdvertisedService(ledService);
  ledService.addCharacteristic(ledCharacteristic);
  BLE.addService(ledService);
  ledCharacteristic.setValue(ledState); // Initialize LED state
  BLE.advertise();

  Serial.println("Bluetooth device active, waiting for connections...");

  // Connect to MQTT broker
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  while (!client.connected()) {
    if (client.connect("ESP32Client", ubidotsToken, "")) {
      Serial.println("Connected to MQTT broker");
      // Sync initial LED state from Ubidots
      syncLedStateFromUbidots();
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying...");
      delay(1000);
    }
  }
  digitalWrite(ledPin, false);
}

void loop() {
  // Handle MQTT messages
  if (client.connected()) {
    client.loop();
  } else {
    reconnect();
  }

  // Handle Bluetooth connections
  BLEDevice central = BLE.central();
  if (central) {
    Serial.print("Connected to central: ");
    Serial.println(central.address());
    while (central.connected()) {
      // Check for BLE LED characteristic value
      if (ledCharacteristic.written()) {
        int value = ledCharacteristic.value();
        if (value == 49) {
          ledState = 1;
          }else{ledState = 0;}
        Serial.println("command");
        Serial.println(value);
        setLedState();
        Serial.print("Received BLE command. LED state: ");
        Serial.println(value);
        // Sync LED state to Ubidots
        sendDataToUbidots();
      }
    }
    Serial.print("Disconnected from central: ");
    Serial.println(central.address());
  }

  // Check Ubidots for incoming commands
//  checkUbidotsCommands();
}

void setLedState() {
  Serial.print("state: ");
  Serial.println(ledState);
  digitalWrite(ledPin, ledState ? HIGH : LOW);
}

void sendDataToUbidots() {
  // Prepare the payload (data to be sent)
  String payload = "{\"light-controller\": " + String(ledState) + "}";
  Serial.print("cloud data: ");
  Serial.print(payload);

  // Publish the payload to Ubidots
  bool is_success = client.publish("/v1.6/devices/home-automation-group-one", payload.c_str());
  Serial.print("is_success: ");
  Serial.println(is_success);
  // Disconnect from MQTT broker
//  client.disconnect();
}

void syncLedStateFromUbidots() {
  // Subscribe to Ubidots variable updates
  client.subscribe("/v1.6/devices/home-automation-group-one/light-controller");

  // Request the current state from Ubidots
  client.publish("/v1.6/devices/home-automation-group-one/light-controller/lv", "");

  // Wait for Ubidots to respond
  delay(2000); // Adjust as needed
}

//void checkUbidotsCommands() {
//  // Handle incoming Ubidots messages
//  client.loop();
//
//  if (client.available()) {
//    // Read the message payload
//    String payload = client.readString();
//
//    // Parse the payload (assuming it's in JSON format)
//    // You might need to use a JSON library for a more robust implementation
//    if (payload.startsWith("{") && payload.endsWith("}")) {
//      // Extract the LED state from the JSON payload
//      int ubidotsLedState = payload["light-controller"].as<int>();
//      Serial.print("value received from cloud: ");
//      Serial.println(ubidotsLedState);
//
//      // Update the local LED state
//      if (ubidotsLedState == 1) {
//        ledState = 1;
//      } else {
//        ledState = 0;
//      }
//
//      // Update the physical LED state
//      setLedState();
//      Serial.println("Received Ubidots command. LED state: " + String(ledState));
//    }
//  }
//}

void reconnect() {
  // Loop until we're reconnected to MQTT
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client", ubidotsToken, "")) {
      Serial.println("Connected to MQTT broker");
      // Sync initial LED state from Ubidots
      syncLedStateFromUbidots();
    } else {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.println(client.state());
      delay(1000);
    }
  }
}
