#include <Arduino.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

//For soil mositure sensors
#define DRY_VALUE 2500
#define WET_VALUE 1250

// WiFi
const char* ssid = "********";
const char* password = "********";
WiFiClient espClient;

// MQTT broker
PubSubClient mqtt(espClient);
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic  = "Garba_Invincible/Irrigation";

unsigned long lastMsgTime = 0;
const long publishInterval = 5000; // Publish sensor data after this amount of milliseconds

//Sensors
const int waterSensor1Pin = 32;
const int waterSensor2Pin = 33;
const int waterSensor3Pin = 36;
const int waterSensor4Pin = 39;

const int soilMoistureSensor1Pin = 34;
const int soilMoistureSensor2Pin = 35;

DHTesp dht;
int dhtPin = 4;

//Actuators
Servo sprayServo;
const int servoPin = 25;
const int pumpRelayPin = 23;

//Others
const float flowRate = 290; // In millisecond per cl
float waterLeft = 10000; //In cl
int waterStage = 0;
int registeredWaterLevels[4] = {2500, 5000, 7500, 10000}; //In cl

float temperature;
float humidity;
float averageSoilMoisture;
bool pumpActive = false;

void connectWiFi()
{
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
}

void connectMQTT()
{
  while (!mqtt.connected())
  {
      Serial.print("Connecting to MQTT...");

      String clientId = "CLientIdRaah" + String(random(10000,99999));
      if (mqtt.connect(clientId.c_str()))
      {
        Serial.println("Connected to broker");
        mqtt.subscribe(mqtt_topic);
        Serial.print("Subscribed to topic: ");
        Serial.println(mqtt_topic);
      }
      else {
        Serial.print("Mqtt connection failed, rc=");
        Serial.println(mqtt.state());
        delay(2000);
    }
  }
}

void mqttMessageCallback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived on topic [");
  Serial.print(topic);
  Serial.print("]: ");

  // Convert payload byte array into a readable string
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println(message);
}

void updateWaterStage(int prevWaterStage)
{
  if (analogRead(waterSensor1Pin) < 4096) //Bottom sensor in the tank
  {
    waterStage = 0;
  }
  else if(analogRead(waterSensor2Pin) < 4096)
  {
    waterStage = 1;
  }
  else if(analogRead(waterSensor3Pin) < 4096)
  {
    waterStage = 2;
  }
  else if(analogRead(waterSensor4Pin) < 4096)
  {
    waterStage = 3;
  }

  if (waterStage != prevWaterStage)
  {
    waterLeft = registeredWaterLevels[waterStage];
  }
}

float getSoilMoisture()
{
  int raw1 = analogRead(soilMoistureSensor1Pin);
  Serial.println(raw1);
  int raw2 = analogRead(soilMoistureSensor2Pin);
  Serial.println(raw2);

  int moisture1 = map(raw1, DRY_VALUE, WET_VALUE, 0, 100);
  int moisture2 = map(raw2, DRY_VALUE, WET_VALUE, 0, 100);

  moisture1 = constrain(moisture1, 0, 100);
  moisture2 = constrain(moisture2, 0, 100);

  return (moisture1 + moisture2)/2.0;
}

void sprayWater(int milliseconds, int angleRange)
{
  long timeBeforePumping = millis();
  //Activate pump
  digitalWrite(pumpRelayPin, LOW);
  Serial.println("Started");
  int stepDelay = milliseconds / angleRange;
  for (int i = 0; i < angleRange; i++)
  {
    sprayServo.write(i);
    delay(stepDelay);
  }

  waterLeft -= flowRate * (millis() - timeBeforePumping);
  digitalWrite(pumpRelayPin, HIGH);
  Serial.println("Stopped");
  sprayServo.write(0);
}

void getTemperatureAndHumidity()
{
  TempAndHumidity newValues = dht.getTempAndHumidity();

  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0)
  {
    Serial.println("DHT error status: " + String(dht.getStatusString()));
    return;
  }

  temperature = newValues.temperature;
  humidity = newValues.humidity;
}

void setup()
{
  Serial.begin(115200);
  pinMode(pumpRelayPin, OUTPUT);

  digitalWrite(pumpRelayPin, LOW);

  connectWiFi();

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttMessageCallback);

  // Attach Servo
  ESP32PWM::allocateTimer(0);
  sprayServo.setPeriodHertz(50); // Standard 50Hz servo
  sprayServo.attach(servoPin, 500, 2400);
  sprayServo.write(0); // Set nozzle to starting angle (0 degrees)

  dht.setup(dhtPin, DHTesp::DHT22);
}

void loop() {

  if (!mqtt.connected())
  {
    connectMQTT();
  }

  mqtt.loop();

  updateWaterStage(waterStage);
  averageSoilMoisture = getSoilMoisture();
  getTemperatureAndHumidity();

  if (averageSoilMoisture <= 30)
  {
    pumpActive = true;
    sprayWater(50*flowRate, 120);
  }
  else
  {
    pumpActive = false;
  }

  //Publish sensor readings
  unsigned long now = millis();

  if ((now - lastMsgTime) > publishInterval)
  {
    lastMsgTime = now;
    char message[64];
    snprintf(message, sizeof(message), "%.6f,%.6f,%.6f,%.6f,%s", temperature, humidity, waterLeft, averageSoilMoisture, pumpActive?"true":"false");

    // Publish to MQTT topic
    mqtt.publish(mqtt_topic, message);

    Serial.print("Published message: ");
    Serial.println(message);
  }
}
