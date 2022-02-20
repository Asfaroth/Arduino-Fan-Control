#include <SPI.h>
#include <Ethernet3.h>
#include <Arduino_JSON.h>
#include <DHT.h>
#include <List.hpp>

struct CurveEntry {
  float starting;
  int factor;
};

List<CurveEntry> curve;
float pwmStart = 0.1f;
int percentPerDegree = 10;

#define DHTPIN 7
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

byte mac[] = {
  0x44, 0x42, 0x62, 0x50, 0x91, 0x02
};
IPAddress defaultIp(10, 0, 0, 2);
EthernetServer server(80);

void setup() {
  // Shine LED_BUILTIN when setting up the Arduino.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Default Settings: linear curve starting from 26° and 10% PWM duty cycle
  CurveEntry defaultCurve = {26.0f, 1};
  curve.addFirst(defaultCurve);

  // Start the Ethernet board and Sensor
  if (Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac, defaultIp);
  }
  server.begin();
  dht.begin();

  // Configure PWM pin 3 and Timer2
  pinMode(3, OUTPUT);
  TIMSK2 = 0;
  TIFR2 = 0;
  TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << WGM22) | (1 << CS21);
  OCR2A = 79;
  OCR2B = 0;

  // Setup done, power off LED_BUILTIN
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  float pwmSignal = 0.0f;

  if (!isnan(temp) && !curve.isEmpty()) {
    if (temp >= curve.getValue(0).starting) { // temp is under threshold, power fans down
      pwmSignal = pwmStart;

      for (int i = 0; i < curve.getSize(); i++) {
        if (curve.getSize() > i + 1 && curve.getValue(i + 1).starting < temp) { // skip current step as only the next one will be relevant again
          pwmSignal += (curve.getValue(i + 1).starting - curve.getValue(i).starting) * curve.getValue(i).factor / percentPerDegree;
        } else {
          pwmSignal += (temp - curve.getValue(i).starting) * curve.getValue(i).factor / percentPerDegree;

          if (pwmSignal > 1.0f) {
            pwmSignal = 1.0f;
            break;
          }
        }
      }
    }
  } else {
    pwmSignal = 1.0f;
  }
  setPWM(pwmSignal);
  
  EthernetClient client = server.available();
  if (client) {
    IPAddress ip = Ethernet.localIP();
    String ipString = "";
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      ipString += String(ip[thisByte], DEC) + ".";
    }

    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n' && currentLineIsBlank) {
          JSONVar response;
          response["DTH22_temp"] = temp;
          response["DTH22_hum"] = hum;
          response["PWM_val"] = pwmSignal;
          response["IP"] = ipString;
          
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/json");
          client.println("Connection: close"); 
          client.println("Refresh: 5");
          client.println();
          client.println(JSON.stringify(response));
          
          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
    delay(1);
    client.stop();
  }
}

void setPWM(float f) {
    f=f<0?0:f>1?1:f;
    OCR2B = (uint8_t)(79*f);
}
