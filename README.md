# Arduino-Fan-Control

This repository contains all data related to my weekend project to build a custom PWM fan controller for my server rack.
The controller is based on the Arduino platform and is able to communicate with the local network via Ethernet.
It measures the temperature, exposes the gathered data via an API and controls a PWM fan according to it.

# Needed parts

- Arduino Nano
- USR-ES1 Mini Ethernet Board (based on WM5500)
- DHT-22 Temperature Sensor
- LD33V 3.3V voltage regulator
- two capacitors (for voltage regulation)
- some perfboard

# Arduino Dependencies

- [Ethernet3](https://github.com/sstaub/Ethernet3)
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library)
- [Arduino List](https://github.com/nkaaf/Arduino-List/)
