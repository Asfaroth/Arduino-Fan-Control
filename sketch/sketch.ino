#include <SPI.h>
#include <Ethernet3.h>
#include <Arduino_JSON.h>
#include <DHT.h>
#include <List.hpp>

struct CurveEntry {
  float starting; // starting temperature when this step should be active
  float factor; // percent per 0.1 degree (so 10% PWM duty cycle increase per 1° would be 0.1f)
};
List<CurveEntry> curve;
float pwmStart = 0.1f;

enum REQUESTTYPE {
  NOT_KNOWN, GET, POST
};
enum REQUESTPATH {
  NOT_FOUND, METRICS, CONFIG
};
struct Request {
  REQUESTTYPE type;
  REQUESTPATH path;
};

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

  // Default Settings: linear curve starting from 26°, 10% per degree
  CurveEntry defaultCurve = {26.0f, 0.1f};
  curve.add(defaultCurve);

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

  /* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ PWM Stuff ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

  if (!isnan(temp) && !curve.isEmpty()) {
    if (temp >= curve.getValue(0).starting) { // temp is under threshold, power fans down
      pwmSignal = pwmStart;

      // calculate PWM duty cycle
      for (int i = 0; i < curve.getSize(); i++) {
        if (i + 1 < curve.getSize() && curve.getValue(i + 1).starting < temp) { // skip current step as only the next one will be relevant again
          pwmSignal += (curve.getValue(i + 1).starting - curve.getValue(i).starting) * curve.getValue(i).factor;
        } else {
          pwmSignal += (temp - curve.getValue(i).starting) * curve.getValue(i).factor;
          break;
        }
      }
    }
  } else {
    pwmSignal = 1.0f;
  }
  if (pwmSignal > 1.0f) { // make sure that the PWM signal is capped at 100%
    pwmSignal = 1.0f;
  }
  setPWM(pwmSignal);
  
  /* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ETH Stuff ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
  
  EthernetClient client = server.available();
  if (client) {
    String reqString = "";
    boolean currentLineIsBlank = true;
    boolean firstLine = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        if (firstLine) {
          reqString += c;
        }

        if (c == '\n' && currentLineIsBlank) {
          Request req = extractRequestInformation(reqString);

          switch (req.type) {
            case GET: {
              switch(req.path) {
                case METRICS: {
                  JSONVar response;
                  response["DTH22_temp"] = temp;
                  response["DTH22_hum"] = hum;
                  response["PWM_val"] = pwmSignal;
                  
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-Type: text/json");
                  client.println("Connection: close"); 
                  client.println("Refresh: 5");
                  client.println();
                  client.println(JSON.stringify(response));
                  
                  break;
                } case CONFIG: {
                  JSONVar response;
                  response["IP"] = getIPString(Ethernet.localIP());
                  
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-Type: text/json");
                  client.println("Connection: close");
                  client.println();
                  client.println(JSON.stringify(response));

                  break;
                } default: {
                  client.println("HTTP/1.1 404 Not Found");
                  client.println("Content-Type: text/html");
                  client.println("Connection: close");
                  client.println();
                  client.println("404 Not Found");

                  break;
                }
              }

              break;
            } case POST: {
              switch (req.path) {
                case CONFIG: {
                  break;
                } default: {
                  client.println("HTTP/1.1 405 Method Not Allowed");
                  client.println("Content-Type: text/html");
                  client.println("Connection: close");
                  client.println();
                  client.println("405 Method Not Allowed");

                  break;
                }
              }

              break;
            } default: {
              client.println("HTTP/1.1 405 Method Not Allowed");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              client.println("405 Method Not Allowed");

              break;
            }
          }
          break;
        }

        if (c == '\n') {
          currentLineIsBlank = true;
        } else if (c != '\r') {
          currentLineIsBlank = false;
        } else { // as soon as the first \r reset firstLine as we got all information what we needed
          firstLine = false;
        }
      }
    }
    delay(1);
    client.stop();
  }
}

/**
 * Method that sets the Timer2 register to the given duty cycle so that the PWM output is adopted.
 * 
 * @param f the new PWM duty cycle
 */
void setPWM(float f) {
    f=f<0?0:f>1?1:f;
    OCR2B = (uint8_t)(79*f);
}

/**
 * Method that transforms a given IP Address object into a String.
 * 
 * @param ip the IP Address that should be transformed
 * @return String the built String representation
 */
String getIPString(IPAddress ip) {
  String ipString = "";
  for (byte i = 0; i < 4; i++) {
    ipString += String(ip[i], DEC);

    if (i < 3) {
      ipString += ".";
    }
  }
  return ipString;
}

Request extractRequestInformation(String firstLine) {
  Request req = {GET, METRICS};

  String buffer = "";
  boolean methodFound = false;
  for (int i = 0; i < firstLine.length(); i++) {
    if (firstLine.charAt(i) == ' ') {
      if (methodFound) {
        if (buffer.equals("/metrics")) {
          req.path = METRICS;
        } else if (buffer.equals("/config")) {
          req.path = CONFIG;
        } else {
          req.path = NOT_FOUND;
        }
        break;
      } else {
        if (buffer.equals("GET")) {
          req.type = GET;
        } else if(buffer.equals("POST")) {
          req.type = POST;
        } else {
          req.type = NOT_KNOWN;
        }
        buffer = "";
        methodFound = true;
      }
    } else {
      buffer += firstLine.charAt(i);
    }
  }

  return req;
}
