#include <ESP8266WiFi.h> // Used for Wifi 
#include <PubSubClient.h> // Used for MQTT
#include <ArduinoJson.h> // Used for Json
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_LIS3DH.h>//Used for accelerometer
#include <Adafruit_Sensor.h>
#include <Adafruit_ADS1015.h>//Used for ADC

//MQTT
const char* ssid = "EEERover"; // Used for Wifi- wifi username
const char* password = "exhibition"; // Used for Wifi - wifi password

const char* mqtt_server = "192.168.0.10"; // MQTT- IP address
const char* mqtt_password = ""; // MQTT
const char* mqtt_topic = "esys/thecloud"; // MQTT

WiFiClient espClient;
PubSubClient client(espClient);

//Accelerometer variables
// Used for software SPI
#define LIS3DH_CLK 13
#define LIS3DH_MISO 12
#define LIS3DH_MOSI 11
// Used for hardware & software SPI
#define LIS3DH_CS 10

Adafruit_LIS3DH lis = Adafruit_LIS3DH();
//define variables for accelerometer
float accele, accelex, accelexabs;
bool accelWarn = false;
float sumaccele = 0;

//sets threshold for acceleration to detect collision
const float accelexThres = 1.3;

//ADC/Distance
Adafruit_ADS1115 ads; //ADC
int16_t distance;
int16_t sumdistance = 0;
int sampletaken = 0;

//LED GPIO Pins
const int Red = 13; //D7
const int Green = 12; //D6
const int Blue = 14; //D5
const int BuzzPin = 15;

//Time
unsigned long LastPublish = 0;
unsigned long CurrentTime = 0;

void setup() {
  //Serial Setup
  Serial.begin(9600);


  //LED Setup
  pinMode(Red, OUTPUT);
  pinMode(Green, OUTPUT);
  pinMode(Blue, OUTPUT);

  //Buzz Setup
  pinMode(BuzzPin, OUTPUT);
  off();

  //set light colour to purple while device is configuring
  purple();

  //ADC Setup
  Serial.println("ADC Start");
  ads.setGain(GAIN_ONE);

  ads.begin();
  //Accelerometer Setup

  if (! lis.begin(0x18))
  {
    Serial.println("Couldnt start I2C connection");
    dirty();
    while (1);
  }
  Serial.println("LIS3DH found and running");

  lis.setRange(LIS3DH_RANGE_4_G);   // 2, 4, 8 or 16 G!

  Serial.print("Range = "); Serial.print(2 << lis.getRange());
  Serial.println("G");

  dirty();
  //MQTT Setup
  setup_wifi();

  //set light colour to purple while device is configuring
  purple();

  client.setServer(mqtt_server, 1883);

}

//set variable to determine how many times device tries to connect with mqtt server
int mqttRetiresLeft = 5;

void loop()
{

  // Make sure we are connected to the central broker for incident reporting
  if (!client.connected() && mqttRetiresLeft > 0)
  {
    reconnect();
    mqttRetiresLeft --;
  } else if (mqttRetiresLeft > 0) { // If connected, keep connection alive
    mqttRetiresLeft = 10;
    client.loop();
  }

  //DistanceDetection via ADC
  distance = (2 * ads.readADC_SingleEnded(0)) / 10;
  if (distance > 5000)
  {
    distance = 5000;
  }

  //Low pass filter of ADC values (sigma) to eliminate noise before printing
  sumdistance = sumdistance + int(distance);
  Serial.print("Distance: "); Serial.print(distance); Serial.println("mm; \n");

  //Accelerometer
  lis.read();      // get X Y and Z data at once
  sensors_event_t event;
  lis.getEvent(&event);
  //print acceleration in z direction
  Serial.print(" \tAcceleration: "); Serial.print(event.acceleration.z);
  Serial.print(" m/s^2 z; x=");
  //print acceleration in x direction
  Serial.print(event.acceleration.x);
  //set acceleration varaiables for collision detection algorithm
  accele = event.acceleration.z;
  accelex = event.acceleration.x;
  sumaccele = sumaccele + accele;

  accelexabs = abs(accelex);

  //checks if object is accelerating greater than threshold
  accelWarn = accelexabs > accelexThres;
  if (accelWarn) {
    Serial.print(" --> WARN");
  }
  Serial.println();

  int redWarnSkips = 0;

  // When distance is less than 50 cm than warn -- alot!
  if (distance < 500)
  {
    //Use high frequency buzzing to indicate extreme danger
    buzzAndFlashRedAlot();
    redWarnSkips = 0; //reset moderate warning counter


  } else if (distance < 1000) // If less than 1m, than object is close
  // Moderate amount of warning
  {

    // Use red to indicate < 1.5m and buzz gently
    red();

    // If turning into obstacle, then warn more
    if (accelWarn) {
      buzzAndFlash();
      red();
    } else {

      // Warn moderately with buzzer - skips 3 cycles before toggling - relies on moderate warning counter
      if (redWarnSkips <= 0) {
        buzz();
        redWarnSkips = 3;
      } else {
        redWarnSkips--;
      }
    }

  }
  else if (distance < 1500) // less than 1.5m, some warning
  {
    // Use red without warning tone
    red();
    //Unless turning
    if (accelWarn) {
      buzzAndFlash();
      red();
    }
    redWarnSkips = 0;
  }
  else if (distance < 2000) // 1.5 < d < 2: take caution
  {
    // use amber to show that caution
    amber();
    //and blink if turning into danger
    if (accelWarn) {
      buzzAndFlash();
      amber();
    }
    redWarnSkips = 0;
  }
  else if (distance < 2500)
  {
    // use amber to show that caution
    amber();
    if (accelWarn) {
      // if turning into obstacle 2.5m away, then only blink - ample time
      delay(200);
      off();
      delay(100);
      amber();
    }
    redWarnSkips = 0;
  }
  else
  {
    // show green further than 2.5m
    green();
    redWarnSkips = 0;
  }

  //If crash detected
  if (abs(accele) > 15)
  {
    // Alert broker for emergency services
    client.publish(mqtt_topic, "crash", true);
    purple();
    Serial.println("CRASHED!");
    delay(10000);
  }

  //Publish to MQTT every second
  CurrentTime = millis();
  sampletaken++;
  if (CurrentTime - LastPublish >= 1000)
  {
    //Form the JSON payload
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    
    root[("accel")] = String(sumaccele / sampletaken);
    root[("distance")] = String(sumdistance / sampletaken);

    //Push to MQTT broker
    String Info;
    root.printTo(Info);
    char charBuf[256];
    Info.toCharArray(charBuf, 256);
    client.publish(mqtt_topic, charBuf , true);
    
    //Reset Low pass filtering accumulator
    sumdistance = 0;
    sumaccele = 0;
    sampletaken = 0;
    
    //Reset timer
    LastPublish = CurrentTime;
  }

  // Slow down processing with 100ms sleep
  delay(100);
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected() && mqttRetiresLeft > 0)
  {
    Serial.print("Attempting MQTT connection...");

    mqttRetiresLeft --;

    if (client.connect("ESP8266Client"))
    {
      Serial.println("connected");

    } else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 1 seconds");
      // Wait 5 seconds before retrying
      delay(1000);
    }
  }
}

int wifiAttemptsLeft = 10;

void setup_wifi() //Working Wifi
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED && wifiAttemptsLeft > 0)
  {
    delay(500);
    Serial.print(".");
    wifiAttemptsLeft --;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiAttemptsLeft = 10;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void off()
{
  digitalWrite(Red, HIGH);
  digitalWrite(Green, HIGH);
  digitalWrite(Blue, HIGH);
}

void red()
{
  off();
  digitalWrite(Red, LOW);
}

void amber()
{
  off();
  digitalWrite(Green, LOW);
  digitalWrite(Red, LOW);
}

void green()
{
  off();
  digitalWrite(Green, LOW);
}

void purple()
{
  off();
  digitalWrite(Red, LOW);
  digitalWrite(Blue, LOW);
}

void dirty()
{
  off();
  digitalWrite(Green, LOW);
  digitalWrite(Blue, LOW);
}

void buzz()
{
  digitalWrite(BuzzPin, HIGH);
  delay(10);
  digitalWrite(BuzzPin, LOW);
  delay(10);
}
void buzzOn() {

  for (int i = 0; i < 10; i++) {
    digitalWrite(BuzzPin, HIGH);
    delay(3);
    digitalWrite(BuzzPin, LOW);
    delay(3);
  }
}
void buzzOff() {

  for (int i = 0; i < 10; i++) {
    digitalWrite(BuzzPin, HIGH);
    delay(1);
    digitalWrite(BuzzPin, LOW);
    delay(1);
  }
}

// Buzz and flash the LED
void buzzAndFlash() {
  buzzOn();
  delay(200);
  off();
  delay(100);
  buzzOff();
}

void buzzAndFlashRedAlot(){
      //initiate red light if objects too close
    red();

    // Very near and about to collide - turning
    // Here comes a lot of buzzing!
    if (accelWarn) {
      //buzz if accelerating towards object that is too close-high danger event
      buzzOn();
    } else {
      buzz();
    }
    
    delay(100);
    
    if (accelWarn) {
      buzzOff();
    } else {
      buzz();
    }
    
    delay(100);
    
    //initiate flashing light for high danger event- switch on and off with delays
    off();
    
    if (accelWarn) {
      buzzOn();
    } else {
      buzz();
    }
    
    delay(100);
    
    if (accelWarn) {
      buzzOff();
    } else {
      buzz();
    }

    red();
}


