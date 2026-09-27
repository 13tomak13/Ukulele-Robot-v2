# Ukulele-Robot-v2

A robotic system that autonomously plays a standard acoustic ukulele.

The robot takes musical notation in **MusicXML (.mxl)** format and converts it into a sequence of commands that control servo-driven fretting and strumming mechanisms.

## Features

* Automatic fretting and strumming
* MusicXML (.mxl) song input
* Programmable song playback
* Custom music-to-motion conversion
* Servo-based mechanical control
* LCD and rotary encoder interface
* EEPROM storage for songs and settings

## Hardware

* Arduino Uno R4 Minima
* 16-channel PWM servo driver
* Multiple servo motors
* Acoustic ukulele
* LCD display
* Rotary encoder

## Software

The project includes the embedded Arduino code and tools used to convert MusicXML musical notation into commands understood by the robot as well as a song book with ready to use robot commands.
