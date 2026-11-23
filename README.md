# Time Capsule Exhibit: From ENIAC to Pi

An interactive museum exhibit controller on a Raspberry Pi Pico, simulated in Wokwi.
A visitor is detected, greeted, and guided through six generations of computer
history on an LCD, with a servo pointing to the current era.

## Features
- PIR motion detection read directly from the GPIO register in ARM assembly
- 1602 I2C LCD driver written from scratch in C
- Servo timeline pointer, era-skip button and potentiometer brightness control
- Standby after 10 seconds of no motion, with protection against false re-triggers
- Active buzzer greeting switched through an S8050 transistor stage

## Pin map
| Part | Pin |
|---|---|
| PIR sensor | GP2 |
| LCD (I2C) | GP4 (SDA), GP5 (SCL) |
| Servo | GP15 |
| Button | GP14 |
| Potentiometer | GP26 |
| Brightness LED | GP16 |
| Buzzer (via transistor) | GP12 |

## Run it
Open in VS Code with the Raspberry Pi Pico and Wokwi extensions, click Compile,
then start the Wokwi simulation and use "Simulate motion" on the PIR.

## Not implemented yet
WS2812 strip animation and visitor statistics.
