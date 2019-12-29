/*
 ESP8266 NodeMCU-Homepoint

 Hardware: Arduino-Touch mit ESP8266 NodeMCU
 Funktion:
 Homepoint fungiert als MQTT-client und empfängt Temperatur per Subscrition.
 SonOff Schalter mit nachgerüsteten Temperatursensor oder TH10 mit 
 geliefertem Sensor, sowie eigene Smarthome Geräte sind anschließbar. 
 Interner Temperatursensor DS18B20 wird über OneWire angesteuert.
 Temperaturwerte werden auf dem angeschlossenen Display ausgegeben.
 Der MQTT-server befindet sich im Netzwerk (z.B Raspberry Server).
 Zum Schalten der SonOff Schalter wurden Touch-Button konfiguriert. 

 Programm erprobt ab Arduino IDE 1.8.10
  
 Notwendige Libraries:
 Adafruit_ILI9341, Adafruit_GFX
 XPT2046_Touchscreen,
 OneWire, DallasTemperature,
 PubSubClient (ver.2.6.0 only),
 esp8266 board-library Ver.2.5.0 (wegen kompatibilität zu tft und time Funktionen)
                       Andere Versionen können Probleme bereiten.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
/* pubsubclient ver.2.6.0 only */
#include <PubSubClient.h>

#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include <simpleDSTadjust.h>

#include <OneWire.h> 
#include <DallasTemperature.h>

#include <XPT2046_Touchscreen.h>
/* mögliche fonts */
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono18pt7b.h>


/* 
 * MQTT & WiFi 
 */
/* change it with your ssid-password, mqtt server ip,.. */
const char* ssid = "MYWLAN";
const char* password = "mywlanpassword";
const char* mqtt_server = "192.168.178.10";
const char* clientId = "esp8266-homepoint";

/* create an instance of WiFi, PubSubClient */
WiFiClient espClient;
PubSubClient client(espClient);

/* topics */
#define TOPIC_SMARTHOME   "smarthome"  /* led/klingel/temp */
#define TOPIC_SONOFF      "sonoff"
#define DEVICE_TH10       "2531"
#define DEVICE_MULTI      "3432"
#define DEVICE_STUBE      "6816"
#define DEVICE_KELLER     "3424"
#define DEVICE_USB        "0616"

typedef struct sonoffDevices
{ 
  const char devName[5];
  const char location[10];
  float temp;
  float humidity;
};

#define MAX_SONOFF_DEVICES     5
#define MAX_SMARTHOME_DEVICES  1

struct sonoffDevices sDevices[MAX_SONOFF_DEVICES + MAX_SMARTHOME_DEVICES] =
{
  {DEVICE_TH10, "Gäste", 100.0, 101.0},
  {DEVICE_STUBE, "Stube", 100.0, 101.0},
  {DEVICE_KELLER, "Keller", 100.0, 101.0},
  {DEVICE_MULTI, "Multi", 100.0, 101.0},
  {DEVICE_USB, "Option", 100.0, 101.0},
  {"ESP", "Klingel", 100.0, 101.0}
};

/* 
 * IR to Mqtt with a simple Remote control (faytech) 
 * included from 'esp8266-ir-mqtt-control' project
 */
#define IRKEY_CAR       69  // Car
#define IRKEY_MULTI     71  // Multi
#define IRKEY_KELLER    64  // Keller
#define IRKEY_STUBE     70  // Stube
#define IRKEY_GAST      21  // Gast
#define IRKEY_USB        9  // Option
#define IRKEY_KLINGEL   25  // Klingel

/*
 * ILI9341 on ArduinoTouch
 */
// Pins for the ILI9341 on ArduinoTouch
#define TFT_CS          D1     // GPIO 16
#define TFT_RST         -1     // kein Port nötig, da Reset an Reset angeschlossen
#define TFT_DC          D2     // GPIO 5
#define TFT_MOSI        D7     // GPIO 13
#define TFT_CLK         D5     // GPIO 14
#define TFT_MISO        D6     // GPIO 12
#define TFT_LED         D8     // GPIO 15
// speaker & key
#define SPEAKER         D0     // GPIO 16 Lautsprecher für Signalton
#define TASTER          9      // GPIO 9 Taster

// adc 0..1,0V  3V3 - 220R - 100R - GND (vout=vin*100/320)
#define ADC_PIN         A0
// adc set to read VCC voltage, or read voltage 0..1,0V if disabled 
ADC_MODE(ADC_VCC);

/* create an instance of display */
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

/* rotation of display */
#define ROTATION_NO      0
#define ROTATION_90      1  // user now
#define ROTATION_180     2
#define ROTATION_270     3

/* Touch */
#define TOUCH_CS         0     // GPIO 0 D3 touch screen chip select
#define TOUCH_IRQ        2     // GPIO 2 D4 touch screen interrupt

#define MINPRESSURE      10
#define MAXPRESSURE      2000
// raw values
#define TS_MINX          230
#define TS_MINY          350
#define TS_MAXX          3700
#define TS_MAXY          3900

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

/*
 * OneWire bus
 */
// Pin for onewire bus
#define ONE_WIRE_BUS    10 // SD3 GPIO10

/* create an instance of onewire */
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define INTERN_LED_BLUE  16

/*
 * Time Date
 */
#define NTP_MIN_VALID_EPOCH 1533081600
// change for different NTP (time servers)
#define NTP_SERVERS "0.ch.pool.ntp.org", "1.ch.pool.ntp.org", "2.ch.pool.ntp.org"
#define UTC_OFFSET +1

// Adjust according to your language
const String WDAY_NAMES[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
const String MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
//const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
//const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600}; // Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 2, 0};       // Central European Time = UTC/GMT +1 hour

/* create an instance of DST */
simpleDSTadjust dstAdjusted(StartRule, EndRule);

/* 
 * --------- Variables --------- 
 */
float merketemperatur=0;
int tsx, tsy, tsxraw, tsyraw, lastX = 0;
bool tsdown = false;
bool dspInit = false;
uint8_t rotation = ROTATION_90;

/*
 * ---------- forward ---------- 
 */
void tft_drawFull(void);
void tft_drawHeader(void);
void tft_drawTempExt(int idx);
void tft_drawTempLocal(float temperatur);

int getADC(void);
int8_t getWifiQuality(void);
void drawWifiQuality(void) ;
void drawTime(void);
int tft_getButton(int x, int y);
void tft_drawLight(int idx, bool powerOn);


/*
 *  ************  MQTT  ****************
 */
void mqttconnect() 
{
  /* Loop until reconnected */
  while (!client.connected()) 
  {
    Serial.println("MQTT connecting");
    Serial.println(mqtt_server);
    /* connect now */
    if (client.connect(clientId)) 
    {
      char buf[16];
      Serial.println("connected");
      /* subscribe topic with default QoS 0*/
      sprintf(buf, "%s/#", TOPIC_SMARTHOME);
      client.subscribe(buf);
      sprintf(buf, "%s/#", TOPIC_SONOFF);
      client.subscribe(buf);
      tone(SPEAKER, 1200,500);
    } 
    else 
    {
      Serial.print("failed, status code =");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      /* Wait 3 seconds before retrying */
      delay(3000);
    }
  }
}

/* send request to all sensors */
void sendInit(void)
{
  char cmd[32];
  for (int i=0; i<MAX_SONOFF_DEVICES; i++)
  {
    sprintf(cmd, "sonoff/%s/cmnd/Status", sDevices[i].devName);
    client.publish(cmd, "8");
  }
}

/* Mqtt publish a command to sonoff switch */
void sendButton(int idx)
{
  char cmd[32];
  sprintf(cmd, "sonoff/%s/cmnd/POWER", sDevices[idx].devName);
  client.publish(cmd, "2");
}

/* Mqtt callback of subscription */
void receivedCallback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message received: ");
  Serial.println(topic);

  Serial.print("payload: ");
  for (int i = 0; i < length; i++) 
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  /* smarthome devices */
  if (strstr(topic, TOPIC_SMARTHOME))
  {
    if (strstr(topic, "temp"))
    {
      sDevices[MAX_SONOFF_DEVICES].temp = atof((const char*)payload);

      Serial.print("Temp: ");
      Serial.print(sDevices[MAX_SONOFF_DEVICES].location);
      Serial.print(" is ");
      Serial.println(sDevices[MAX_SONOFF_DEVICES].temp);

      tft_drawTempExt(MAX_SONOFF_DEVICES);
    }
    else if (strstr(topic, "ir"))
    {
      int irKey = 0;
      /* 
       * if included from 'esp8266-ir-mqtt-control' project
       * NEC 16754775,0,21 
       */
      char *pP = strstr((const char*)payload, ",");
      if (pP)
      {
        char *pB = strstr((const char*)payload, ",");
        irKey = atoi(pB+1);
        Serial.print("irKey: ");
        Serial.println(irKey);
        //keyPressed(irKey);
      }
    }
  }
  /* sonoff devoces */
  if (strstr(topic, TOPIC_SONOFF))
  {
    float hum = 101.0;
    char *pP = strstr((const char*)payload, "Humidity");
    if (pP)
    {
      char *pB = pP + 10;
      pP = strstr(pB, "},");
      if (pP)
      {
        *pP = 0;
        hum = atof((const char*)pB);
      }
    }
    pP = strstr((const char*)payload, "Temperature");
    if (pP)
    {
      char *pB = pP + 13;
      pP = strstr(pB, ",\"");
      if (pP)
      {
        *pP = 0;
        for (int i=0; i<MAX_SONOFF_DEVICES; i++)
        {
          pP = (char*)sDevices[i].devName;
          if (strstr(topic, pP))
          {
            sDevices[i].temp = atof((const char*)pB);
            Serial.print("Temp: ");
            Serial.print(sDevices[i].location);
            Serial.print(" is ");
            Serial.print(sDevices[i].temp);
            if (hum < 101.0)
            {
              sDevices[i].humidity = hum;
              Serial.print(" Hum: ");
              Serial.print(sDevices[i].humidity);
              Serial.println();
            }

            tft_drawTempExt(i);
            break;
          }
        }
      }
    }
    /* sonoff/3432/RESULT payload: {"POWER":"OFF"} */
    if (strstr(topic, "RESULT"))
    {
      bool powerOn = false;
      pP = strstr((const char*)payload, "POWER\":\"OFF");
      if (! pP)
      {
        pP = strstr((const char*)payload, "POWER\":\"ON");
        if (pP)
          powerOn = true;
      }
      if (pP)
      {
        for (int i=0; i<MAX_SONOFF_DEVICES; i++)
        {
          pP = (char*)sDevices[i].devName;
          if (strstr(topic, pP))
          {
            tft_drawLight(i, powerOn);
            break;
          }
        }
      }
    }
  }
}

/*
 *  Main entry
 */
void setup() 
{
  pinMode(TASTER, INPUT);  // Port aus Ausgang schalten
  DS18B20.begin();         // lokale Temperaturmessung
    
  Serial.begin(115200);
  
  int heap = ESP.getFreeHeap();
  Serial.printf("RAM available %i\n",heap);
  
  tft.begin();
  tft.setRotation(rotation);
  tft.setTextWrap(false);
  yield();
  tft.fillScreen(ILI9341_BLACK);
  yield();
  tft.setTextSize(1);
  //tft.setFont(&FreeSans9pt7b);
  tft.setFont(&FreeMono9pt7b);
  
  // Background display
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);    // HIGH to Turn on;
  // on board led off
  pinMode(INTERN_LED_BLUE, OUTPUT);
  digitalWrite(INTERN_LED_BLUE, HIGH); // HIGH to Turn off;

  Serial.print("tftx ="); Serial.print(tft.width()); Serial.print(" tfty ="); Serial.println(tft.height());

  tft_drawFull();

  touch.begin();
  //aktuelle Werte zurücksetzen
  tsx = 0;
  tsy = 0;
  tsxraw = 0;
  tsyraw = 0;
  tsdown = false;

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Verbunden mit ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  /* configure the MQTT server with IPaddress and port */
  client.setServer(mqtt_server, 1883);
  /* this receivedCallback function will be invoked 
  when client received subscribed topic */
  client.setCallback(receivedCallback);
}


int cnt = 0, tmr = 0;
void loop(void)
{
  int taste = digitalRead(TASTER); // Test Taster
 
  /* if client was disconnected then try to reconnect again */
  if (!client.connected()) 
  {
    mqttconnect();

    digitalWrite(INTERN_LED_BLUE, HIGH); // HIGH to Turn off;
    tft_drawHeader();
    sendInit();  // Mqtt request to sensors
  }
  
  tsdown = handleTouch(); 

  /* application of touch here */
   
  /* 
   * this function will listen for incomming 
   * subscribed topic-process-invoke receivedCallback 
   */
  client.loop();

  delay(100);  

  if (cnt++ > 30)
  {
    float temperatur = getTemperatur();
    if (merketemperatur!=temperatur) 
    {
       tft_drawTempLocal(temperatur);
       merketemperatur=temperatur;
    }
    cnt = 0;
  }
  if (tmr++ > 600)
  {
    tft_drawHeader();
    tmr = 0;
  }
}

/*
 *  ************  Touch  ****************
 */
void keyPressed(int key)
{
  Serial.print("Key pressed: ");
  Serial.println(key);
  switch(key)
  {
    case IRKEY_GAST: // Gast
      sendButton(0);
      break;
    case IRKEY_STUBE: // Stube
      sendButton(1);
      break;
    case IRKEY_KELLER: // Keller
      sendButton(2);
      break;
    case IRKEY_MULTI: // Multi
      sendButton(3);
      break;
    case IRKEY_USB: // Option
      sendButton(4);
      break;
    case IRKEY_KLINGEL: // Klingel
      sendButton(5);
      break;
    case IRKEY_CAR: // Car
    default:
      break;
  }
}

// aktuelle Position und Berührungszustand
// vom Touchscreen ermitteln
bool handleTouch() 
{
  bool tsd = false;
  TS_Point p = touch.getPoint(); //aktuelle Daten lesen
  tsxraw = p.x; //x und y als Rohwerte merken
  tsyraw = p.y;
  delay(1);
  //Bildschirm Ausrichtung ermitteln
  uint8_t rot = tft.getRotation();
  //je nach Ausrichtung relative Bildschirmpositionen
  //ermitteln
  switch (rot) {
    case 0: tsx = map(tsyraw, TS_MINY, TS_MAXY, 240, 0);
            tsy = map(tsxraw, TS_MINX, TS_MAXX, 0, 320);
            break;
    case 1: tsx = map(tsxraw, TS_MINX, TS_MAXX, 0, 320);
            tsy = map(tsyraw, TS_MINY, TS_MAXX, 0, 240);
            break;
    case 2: tsx = map(tsyraw, TS_MINY, TS_MAXY, 0, 240);
            tsy = map(tsxraw, TS_MINX, TS_MAXX, 320, 0);
            break;
    case 3: tsx = map(tsxraw, TS_MINX, TS_MAXX,320, 0);
            tsy = map(tsyraw, TS_MINY, TS_MAXY, 240, 0);
            break;
  }

  if (p.z > MINPRESSURE)
  {
    tsd = true;
    Serial.print("Touch: X");         
    Serial.print(tsx); 
    Serial.print(" Y");         
    Serial.print(tsy); 
    Serial.print(" Z");         
    Serial.println(p.z);
    int key = tft_getButton(tsx, tsy);
    keyPressed(key);
  }
  else lastX = 0;

  return tsd;
}

/*
 *  ************  Messungen  ****************
 */
float getTemperatur(void) 
{
  float temp;
  int timeout;

  timeout=30;
  do {
    DS18B20.requestTemperatures(); 
    temp = DS18B20.getTempCByIndex(0);
    delay(100);
    timeout--;
    if (timeout<0) temp=99.9;
  } while (temp == 85.0 || temp == -127.0) ;

  // Temperatur Korrektur
  temp=temp-0.5;
  
  return temp;
}

int getADC(void)
{
    int sValue = analogRead(ADC_PIN);
    Serial.print("ADC: ");
    Serial.println(sValue);
    return sValue;
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality(void) 
{
  int32_t dbm = WiFi.RSSI();
  if(dbm <= -100) {
      return 0;
  } else if(dbm >= -50) {
      return 100;
  } else {
      return 2 * (dbm + 100);
  }
}

/*
 *  ************  Draw TFT  ****************
 */

/* Draw WiFi */
void drawWifiQuality(void) 
{
  int8_t quality = getWifiQuality();
  getADC();

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(250, 16);
  tft.print(String(quality) + "%");
  
  for (int8_t i = 0; i < 8; i++) {
    for (int8_t j = 0; j < 2 * (i + 1); j++) {
      if (quality > i * 12 || j == 0) {
        tft.drawPixel((295 + (2 * i)), (18 - j), ILI9341_WHITE);
      }
    }
  }
}

/* Draw DateTime */
void drawTime(void)
{
  char time_str[11];
  time_t now, dstOffset;
  struct tm * timeinfo;

  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  while((now = time(nullptr)) < NTP_MIN_VALID_EPOCH) 
  {
    Serial.print(".");
    delay(300);
  }
  timeinfo  = localtime (&now);
  dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - now;

  String date = WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_mday) + "." + MONTH_NAMES[timeinfo->tm_mon] + "." + String(1900 + timeinfo->tm_year);
  sprintf(time_str, " %02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min);
  date += String(time_str);

  Serial.print("Time: ");
  Serial.print(timeinfo->tm_hour);
  Serial.print(":");
  Serial.println(timeinfo->tm_min);
  Serial.print("Time: difference for DST:");
  Serial.println(dstOffset);

  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor( 10,16);
  tft.print(date);
}

/* Draw head line, date, time, WiFi */
void tft_drawHeader(void)
{
  for (int i=0; i<24; i++)
    tft.drawFastHLine(0, i, tft.width()-1, ILI9341_BLACK);
  tft.drawRoundRect(0, 0, tft.width()-1,23, 5, ILI9341_YELLOW);
  drawTime();
  drawWifiQuality();
}

/*   Display  320 x 240

Header:    0                                                  319
              10,18 Time      280,8 Quality   300,18 Bar
          23
TempLoc:  24
                 20,50 Temp
          88
TempExt:  90
                18,108 Location
                 20,130 Temp1  
         154
Icons:   155
                170 Radius 5
         185
Buttons: 186
                  3 Nuttons
         239
*/

/* Draw 3 buttons */
void tft_drawButtons(void)
{
  tft.setTextColor(ILI9341_WHITE); 
  /* Gast */
  tft.fillRoundRect(0, 186, 105, 52, 10, ILI9341_OLIVE); 
  tft.drawRoundRect(0, 186, 105, 52, 10, ILI9341_CYAN);
  tft.setCursor( 30,215);
  tft.print("Gast");
  /* Stube */
  tft.fillRoundRect(106, 186, 105, 52, 10, ILI9341_OLIVE);
  tft.drawRoundRect(106, 186, 105, 52, 10, ILI9341_CYAN);
  tft.setCursor( 136,215);
  tft.print("Stube");
  /* Multi */
  tft.fillRoundRect(212, 186, 105, 52, 10, ILI9341_OLIVE);
  tft.drawRoundRect(212, 186, 105, 52, 10, ILI9341_CYAN);
  tft.setCursor( 242,215);
  tft.print("Multi");
}

/* get pressed button */
int tft_getButton(int x, int y)
{
  if ((x>5)&&(x<100)&&(y>190))
  {
    x = 50; y = 200;
    if (lastX != x)
    {
      lastX = x;
      return IRKEY_GAST;
    }
  }
  else if ((x>110)&&(x<205)&&(y>190))
  {
    x = 180; y = 200;
    if (lastX != x)
    {
      lastX = x;
      return IRKEY_STUBE;
    }
  }
  else if ((x>220)&&(x<310)&&(y>190))
  {
    x = 280; y = 200;
    if (lastX != x)
    {
      lastX = x;
      return IRKEY_MULTI;
    }
  }
  return -1;
}

/* Action dot of Buttons */
void tft_drawLight(int idx, bool powerOn)
{
  int pos;
  if (strstr(sDevices[idx].devName, DEVICE_TH10))
    pos = 50;
  else if (strstr(sDevices[idx].devName, DEVICE_STUBE))
    pos = 156;
  else if (strstr(sDevices[idx].devName, DEVICE_MULTI))
    pos = 262;
  else 
    return;
  if (powerOn) {
    tft.fillCircle(pos, 170, 7, ILI9341_ORANGE);
  }
  else {
    tft.fillCircle(pos, 170, 7, ILI9341_BLACK);
  }
}

/* Draw external temperature of Mqtt */
void tft_drawTempExt(int idx)
{
  char temperaturStr[6];
  float temperatur;
  uint16_t textfarbe;

  if (dspInit == false)
  {
    tft.drawRoundRect(0, 90, tft.width()-1, 64, 5, ILI9341_GREEN);

    tft.setTextColor(ILI9341_WHITE); 
    tft.setCursor( 10,106);
    tft.print("Gast Stube Keller Multi Car");
  }
  for (int i=107; i<137; i++)
    tft.drawFastHLine(10, i, tft.width()-21, ILI9341_BLACK);
  
  int pos = 10;
  for (int i=0; i<MAX_SONOFF_DEVICES+MAX_SMARTHOME_DEVICES; i++)
  {
    if (i == 4) // USB
      continue;

    temperatur = sDevices[i].temp;
    
    tft.setCursor(pos,136);
    pos += (tft.width()-20)/5 + 4;

    if (temperatur >= 100.0)
      continue;

    if (temperatur>25) textfarbe=ILI9341_RED;
    else if (temperatur>22) textfarbe=ILI9341_ORANGE;
    else if (temperatur>18) textfarbe=ILI9341_YELLOW;
    else if (temperatur>7) textfarbe=ILI9341_CYAN;
    else if (temperatur>0) textfarbe=ILI9341_LIGHTGREY;
    else textfarbe=ILI9341_WHITE;  

    dtostrf(temperatur, 4, 1, temperaturStr);

    tft.setFont(&FreeMono9pt7b);
    tft.setTextColor(textfarbe, ILI9341_BLACK ); 
    tft.print(temperaturStr);
  }
  tft.setFont(&FreeMono9pt7b);
}

/* Draw local temperature of OneWire */
void tft_drawTempLocal(float temperatur)
{
  uint16_t textfarbe;
  char temperaturStr[6];

  if (dspInit == false)
  {
    tft.drawRoundRect(0, 24, tft.width()-1,64, 5, ILI9341_GREEN);

    tft.setTextColor(ILI9341_WHITE); 
    tft.setCursor( 10,40);     
    tft.print("Innentemperatur C");
  }

  if (temperatur>25) textfarbe=ILI9341_RED;
  else if (temperatur>22) textfarbe=ILI9341_ORANGE;
  else if (temperatur>18) textfarbe=ILI9341_YELLOW;
  else if (temperatur>7) textfarbe=ILI9341_CYAN;
  else if (temperatur>0) textfarbe=ILI9341_LIGHTGREY;
  else textfarbe=ILI9341_WHITE;  

  for (int i=41; i<71; i++)
    tft.drawFastHLine(10, i, tft.width()-21, ILI9341_BLACK);

  dtostrf(temperatur, 4, 1, temperaturStr);

  tft.setFont(&FreeMono18pt7b);
  tft.setTextColor(textfarbe, ILI9341_BLACK ); 
  tft.setCursor(20,70);
  tft.print(temperaturStr);
  tft.setFont(&FreeMono9pt7b);
}

/* Draw full screen on start */
void tft_drawFull(void)
{
  tft.fillScreen(ILI9341_BLACK);
  dspInit = false; 
  tft_drawHeader();
  tft_drawTempLocal(getTemperatur());
  tft_drawTempExt(0);
  tft_drawButtons(); 
  dspInit = true; 
}
