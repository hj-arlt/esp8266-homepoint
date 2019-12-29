# esp8266-homepoint
Smarthome Homepoint with Arduino-Touch and ESP8266 NodeMCU 
controls peripheral devices of Sonoff using Mqtt..

 ESP8266 NodeMCU-Homepoint

 Hardware: ArduiTouch mit ESP8266 NodeMCU
           https://www.az-delivery.de/products/arduitouch-wandgehauseset-mit-touchscreen-fur-esp8266-und-esp32?_pos=7&_sid=cb2e3886c&_ss=r&gclid=EAIaIQobChMIu5T4sKXb5gIVy-R3Ch1irQ1FEAAYASAAEgIVo_D_BwE

           NodeMCU:  
           https://www.az-delivery.de/products/nodemcu?gclid=EAIaIQobChMI9daC86Xb5gIVDOd3Ch0EhwGFEAAYAiAAEgJBTPD_BwE

 Funktion:
 Homepoint fungiert als MQTT-client und empfängt Temperatur per Subscrition.
 SonOff Schalter mit nachgerüsteten Temperatursensor oder TH10 mit 
 geliefertem Sensor, sowie eigene Smarthome Geräte sind anschließbar. 
 Interner Temperatursensor DS18B20 wird über OneWire angesteuert.
 Temperaturwerte werden auf dem angeschlossenen Display ausgegeben.
 Der MQTT-server befindet sich im Netzwerk (z.B Raspberry Server).
 Zum Schalten der SonOff Schalter wurden Touch-Button konfiguriert. 

 Software / IDE
            https://www.arduino.cc/en/main/software
            
 Programm erprobt ab Arduino IDE 1.8.10
  
 Notwendige Libraries:
 Adafruit_ILI9341, Adafruit_GFX
 XPT2046_Touchscreen,
 OneWire, DallasTemperature,
 PubSubClient (ver.2.6.0 only),
 esp8266 board-library Ver.2.5.0 (wegen kompatibilität zu tft und time Funktionen)
                       Andere Versionen können Probleme bereiten.
