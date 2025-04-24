/*
  CO2 Alarm Device

  Author: Lovro Majtan
  Student Number: L00165997

  Displays Carbon Dioxide, temperature and humidity readings on an LCD
  screen while issuing appropriate warnings based on the CO2 level,
  using an RGB LED and a passive buzzer. ESP8266-01 Wi-Fi module communicates
  with a ThingSpeak database and transfers relevant data to it when appropriate.
  Winsen's MH-Z16 paired with sandbox_electronics' I2C/UART interface reads CO2
  value and communicates with Arduino using I2C protocol. DHT11 module is
  responsible for reading temperature and humidity values.
*/

// librares
#include <LiquidCrystal.h> // LCD library
#include <dht_nonblocking.h> // DHT11 library
#include <SoftwareSerial.h> // library allowing communication using the rest of the digital pins
#include <Wire.h> // library allowing communications with I2C devices
#include <NDIR_I2C.h> // MH-Z16 library
#include "pitches.h" // buzzer library
#include <SimpleTimer.h> // library used to sound buzzer for appropriate amout of time

// constant values
#define DHT_SENSOR_TYPE DHT_TYPE_11

// RGB LED pins
#define BLUE 4
#define GREEN 5
#define RED 6

#define BUZZER 14
#define RX 10 // ESP8266 TX connected to pin 10
#define TX 9 // ESP8266 RX connected to pin 9

SoftwareSerial esp8266(RX, TX); // ESP8266 initialisation
NDIR_I2C co2_sensor(0x4D); // MH-Z16 initialisation using I2C protocol

static const int DHT_SENSOR_PIN = 2;
DHT_nonblocking dht_sensor( DHT_SENSOR_PIN, DHT_SENSOR_TYPE ); // DHT11 initialisation

SimpleTimer timer;

// these values allow proper deletion of timer object and handling of the buzzer system
int timerId = -1;
int lastCO2Threshold = -2;

String WIFI_SSID = "WIFI_NAME"; // Wi-Fi to connect to
String WIFI_PASS = "WIFI_PASSWORD"; // Wi-Fi password to access desired network
String HOST = "api.thingspeak.com";
String API = "API_KEY"; // ThingSpeak API key used to send updates to correct database
String PORT = "80"; // HTTP port
int countTrueCommand; // to keep track of successfully issued commands to ESP8266 in serial monitor
boolean found = false; // to check if there is a response while issuing command to ESP8266   

int co2Level = 0; // helps handle blinking of LED

LiquidCrystal lcd(7, 8, 3, 13, 11, 12); // defines LCD with numbers of pins it is connected to

// program setup 
void setup() {
  lcd.begin(16, 2); // initialises LCD with 16 columns, 2 rows
  Serial.begin(115200); // baud rate for communication between Arduino and PC
  esp8266.begin(9600); // baud rate for communication between Arduino and ESP8266

  // RGB LED setup
  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  // RGB to glow blue when device is booting up
  analogWrite(RED, 0);
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 255);

  connectToWiFi(); // function which connects ESP8266 to the chosen Wi-Fi if available

  co2_sensor.begin(); // CO2 sensor starts reading in values
}

// temperature and humidity measured every 3 seconds:
static bool measureEnvironment(float *temperature, float *humidity)
{
  static unsigned long measurementTimestamp = millis();

  if(millis() - measurementTimestamp > 3000ul)
  {
    if(dht_sensor.measure(temperature, humidity) == true)
    {
      measurementTimestamp = millis();
      return true;
    }
  }

  return false;
}


// main program loop
void loop()
{
  // variables that help with handling DHT11 values displayed, buzzer system and ThingSpeak updates
  float temperature;
  float humidity;
  static long buzzerDuration = 0;
  static long lastSendTime = 0;
  long sendInterval = 15000;
  int currentCO2Threshold = -1;

  lcd.setCursor(0, 0);

  timer.run();

  // alert system and ThingSpeak updates logic
  if(co2_sensor.measure())
  {
    co2Level = co2_sensor.ppm;

    lcd.print(co2_sensor.ppm);
    lcd.print("  ");
    lcd.print("ppm");
    lcd.print("  ");

    if(co2_sensor.ppm < 1000)
    {
      currentCO2Threshold = 0;
      noTone(14);
      analogWrite(RED, 0);
      analogWrite(GREEN, 255);
      analogWrite(BLUE, 0);
    }
    else if(co2_sensor.ppm >= 1000 && co2_sensor.ppm < 2500)
    {
      currentCO2Threshold = 1;
      ledBlink(co2_sensor.ppm);
    }
    else
    {
      if(co2_sensor.ppm >= 2500 && co2_sensor.ppm < 5000)
      {
        currentCO2Threshold = 2;
        analogWrite(RED, 255);
        analogWrite(GREEN, 100);
        analogWrite(BLUE, 0);
      }
      else if(co2_sensor.ppm >= 5000 && co2_sensor.ppm < 10000)
      {
        currentCO2Threshold = 3;
        ledBlink(co2_sensor.ppm);
      }
      else if(co2_sensor.ppm >= 10000 && co2_sensor.ppm < 30000)
      {
        currentCO2Threshold = 4;
        analogWrite(RED, 255);
        analogWrite(GREEN, 0);
        analogWrite(BLUE, 0);
      }
      else if(co2_sensor.ppm >= 30000)
      {
        currentCO2Threshold = 5;
        ledBlink(co2_sensor.ppm);
      }

      // update issues to ThingSpeak every 15 seconds after CO2 is above 2,500
      if(millis() - lastSendTime >= sendInterval)
      {
        sendToThingSpeak(temperature, humidity, co2_sensor.ppm);
        lastSendTime = millis();
      }
    }

    // buzzer system logic
    if(currentCO2Threshold != lastCO2Threshold)
      {
        // timer deleted each time CO2 threshold changes so that new timer can be set
        if(timerId != -1)
        {
          timer.deleteTimer(timerId);
          timerId = -1;
        }

        // buzzer sound every time CO2 threshold changes immediately to help with delay caused by using timer object
        buzzerManager();

        switch(currentCO2Threshold)
        {
          case 2:
            timerId = timer.setInterval(10000, buzzerManager);
            break;
          case 3:
            timerId = timer.setInterval(5000, buzzerManager);
            break;
          case 4:
            timerId = timer.setInterval(1000, buzzerManager);
            break;
          case 5:
            timerId = timer.setInterval(1000, buzzerManager);
            break;
        }
        lastCO2Threshold = currentCO2Threshold; // prevents timer being reset over and over during same CO2 threshold
      }
  }
  // if the functions return true, then measurements are available
  if(measureEnvironment(&temperature, &humidity ) == true)
  {
    // prints DHT11 sensor data with appropriate labels
    lcd.setCursor(0, 1);
    lcd.print( "T = " );
    lcd.print(temperature, 0);
    lcd.print((char)223);
    lcd.print("C" );
    lcd.print(" H = ");
    lcd.print(humidity, 0);
    lcd.print("%");
  }
}

// method that sounds buzzer for approptiate amout of time based on CO2 threshold
void buzzerManager()
{
  static long buzzerDuration = 0;

  if(co2Level >= 1000 && co2Level < 5000)
  {
    buzzerDuration = 2000;
  }
  if(co2Level >= 5000 && co2Level < 10000)
  {
    buzzerDuration = 1000;
  }
  if(co2Level >= 10000)
  {
    buzzerDuration = 500;
  }
  tone(14, NOTE_C5, buzzerDuration);
}

// method with logic for led blinking
void ledBlink(int co2Level)
{
  static long ledStartTime = 0;
  static bool ledOn = false;

  // led turns on and off every half a second
  if(millis() - ledStartTime >= 500)
  {
    ledOn = !ledOn;
    ledStartTime = millis();
  
    if(ledOn)
    {
      // if CO2 is above 30,000ppm LED blinks red, else LED blinks orange
      if(co2Level >=30000 || co2Level <= 0) // handling for if co2Level variable goes into negative values due to how sensor reacts when exposed to over 50,000ppm of CO2
      {
        analogWrite(RED, 255);
        analogWrite(GREEN, 0);
        analogWrite(BLUE, 0);
      }
      else
      {
        analogWrite(RED, 255);
        analogWrite(GREEN, 100);
        analogWrite(BLUE, 0);
      }
    }
    else
    {
      analogWrite(RED, 0);
      analogWrite(GREEN, 0);
      analogWrite(BLUE, 0);
    }
  }
}

// method used in setup to connect ESP8266 to chosen Wi-Fi:
void connectToWiFi()
{
  sendCommand("AT", 2000, "OK"); // checks that communication with ESP8266 works
  sendCommand("AT+CWMODE=1", 2000, "OK"); // sets ESP8266 into mode for connection with available access points
  sendCommand("AT+CWJAP=\""+ WIFI_SSID +"\",\""+ WIFI_PASS +"\"",10000,"OK"); // attempts connection with chosen access point
}

// method to send data to chosen ThingSpeak database:
void sendToThingSpeak(float temperature, float humidity, int co2)
{
  // HTTP GET request string that updates database fields using API key to access database
  String request = "GET /update?api_key=" + API + "&field1=" + String(temperature) + 
  "&field2=" + String(humidity)  + "&field3=" + String(co2) + "\r\n"; 

  sendCommand("AT+CIPSTART=\"TCP\",\"" + HOST + "\"," + PORT, 4000, "OK"); // opens TCP connection with ThingSpeak server on port 80 for HTTP
  sendCommand("AT+CIPSEND=" + String(request.length() + 2), 2000, ">"); // bytes to be sent, lenght of request plus 2 for carriage return and line feed
  esp8266.print(request); // ESP8266 sends HTTP GET request to to ThingSpeak
  delay(1500); // 1.5 second delay allows ThingSpeak server to process request
  sendCommand("AT+CIPCLOSE", 2000, "OK"); // close TCP connection to server after data is sent
}

/* 
  method to issue a single command to ESP8266
  command - instruction to be issued to ESP8266
  maxTime - how long ESP8266 has to perform command and return response
  readResponse[] - expected response from ESP8266
*/
void sendCommand(String command, int maxTime, char readResponse[])
{
  Serial.print(countTrueCommand);
  Serial.print(". at command =>");
  Serial.print("");
  esp8266.println(command); // command issued to ESP8266
  long startTime = millis();

  // gives ESP8266 time to process the command and respond
  while(millis() - startTime < maxTime)
  {
    if(esp8266.find(readResponse))
    {
      found = true;
      break;
    }
  }
  if(found == true)
  {
    Serial.println(command + ": OK");
    countTrueCommand++;
  }
  if(found == false)
  {
    Serial.print(command + ": FAIL");
  }
  found = false;
}