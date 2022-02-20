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
  NOT_KNOWN, GET, POST, BAD
};
enum REQUESTPATH {
  NOT_FOUND, METRICS, CONFIG
};
struct Request {
  REQUESTTYPE type;
  REQUESTPATH path;
  int contentLength;
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
    Request req = {BAD, NOT_FOUND, 0}; // will be initialized with the very first line from the HTTP Request

    String reqString = "";
    boolean currentLineIsBlank = true;
    boolean firstLine = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        if (c == '\n' && currentLineIsBlank) {
          switch (req.type) {
            case GET: {
              switch(req.path) {
                case METRICS: {
                  JSONVar response;
                  response["DTH22_temp"] = temp;
                  response["DTH22_hum"] = hum;
                  response["PWM_val"] = pwmSignal;
                  
                  generateAnswer("200 OK", client);
                  client.println(JSON.stringify(response));
                  
                  break;
                } case CONFIG: {
                  JSONVar curveJSON;
                  for (int i = 0; i < curve.getSize(); i++) {
                    JSONVar entry;
                    entry["starting"] = curve.getValue(i).starting;
                    entry["factor"] = curve.getValue(i).factor;

                    curveJSON[i] = entry;
                  }

                  JSONVar response;
                  response["IP"] = getIPString(Ethernet.localIP());
                  response["pwm_start"] = pwmStart;
                  response["curve"] = curveJSON;
                  
                  generateAnswer("200 OK", client);
                  client.println(JSON.stringify(response));

                  break;
                } default: {
                  generateAnswer("404 Not Found", client);

                  break;
                }
              }

              break;
            } case POST: {
              switch (req.path) {
                case CONFIG: {
                  String body = "";
                  for (int i = 0; i < req.contentLength; i++) {
                    body += (char)client.read();
                  }

                  // TODO: parse String and adopt into Settings

                  generateAnswer("200 OK", client);
                  client.println(body);

                  break;
                } default: {
                  generateAnswer("405 Method Not Allowed", client);

                  break;
                }
              }

              break;
            } case BAD: {
              generateAnswer("400 Bad Request", client);

              break;
            } default: {
              generateAnswer("405 Method Not Allowed", client);

              break;
            }
          }
          break;
        }

        if (c == '\n') {
          currentLineIsBlank = true;
          reqString = "";
        } else if (c != '\r') {
          currentLineIsBlank = false;
          reqString += c;
        } else { // \r signals a line ending, check here if the header is relevant
          if (firstLine) {
            req = extractRequestInformation(reqString);

            firstLine = false;
          }

          reqString.toLowerCase();
          if (reqString.startsWith("content-length:")) {
            reqString.replace("\"", "");
            reqString.remove(0, 16);
            req.contentLength = reqString.toInt();
          }
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

/**
 * Method that interprets the first line of an HTTP request in order to map it to the corrseponding enums.
 * 
 * @param firstLine the String representation of the first HTTP request line.
 * @return Request struct object that caontains the needed request data to process it further
 */
Request extractRequestInformation(String firstLine) {
  Request req = {NOT_KNOWN, NOT_FOUND, 0};

  String buffer = "";
  boolean methodFound = false;
  for (int i = 0; i < firstLine.length(); i++) {
    if (firstLine.charAt(i) == ' ') {
      if (methodFound) {
        if (buffer.equals("/metrics")) {
          req.path = METRICS;
        } else if (buffer.equals("/config")) {
          req.path = CONFIG;
        }
        break;
      } else {
        if (buffer.equals("GET")) {
          req.type = GET;
        } else if(buffer.equals("POST")) {
          req.type = POST;
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

void generateAnswer(String answer, EthernetClient client) {
  client.println("HTTP/1.1 " + answer);
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
}
