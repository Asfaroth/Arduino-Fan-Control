#include <SPI.h>
#include <Ethernet3.h>
#include <Arduino_JSON.h>
#include <DHT.h>;

#define DHTPIN 7
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

byte mac[] = {
  0x44, 0x42, 0x62, 0x50, 0x91, 0x02
};
IPAddress defaultIp(10, 0, 0, 2);
EthernetServer server(80);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  if (Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac, defaultIp);
  }
  server.begin();
  dht.begin();

  pinMode(3, OUTPUT);
  TIMSK2 = 0;
  TIFR2 = 0;
  TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << WGM22) | (1 << CS21);
  OCR2A = 79;
  OCR2B = 0;

  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  float pwmSignal = 0.0;

  if (isnan(temp)) {
    pwmSignal = 1.0f;
  } else if (temp > 26.0f) { // begin at 26°, so that the PWM duty cycle begins at 10%. Below that the fan won't move either way.
    if (temp > 35.0f) {
      pwmSignal = 1.0f;
    } else {
      pwmSignal = (temp - 25.0f) / 10.0f;
    }
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
