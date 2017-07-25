# BeyondChaos Flight
This project aims to create open-source flight controller and ground controller for Linux based drones.

## flight
The 'flight' subfolder contains the flight controller itself, currently supporting only Raspberry Pi board, but can be easily extended to any kind of board (including non-Linux ones, but it is not the purpose of this project).

## controller_pc
Highly configurable GUI client that allow you to access any controls over flight controller, see livestream, and fly it.

## controller_rc
This subfolder contains a ground controller designed to be used in an RC remote control. Currently only supporting RaspberryPi board.

## fpviewer
Livestream viewer supporting stereo mirroring and head-up display overlay. Designed to be used with any HDMI goggles (tested with OSVR).

## libcontroller
Contains everything needed to communicate with the drone from ground.

## librawwifi
This is inspired from befinitiv's wifibroadcast, it allows to directly use WiFi in analog-like transmission.

Alternatively, hostapd can be used on the drone and wpa_supplicant on the controller to allow TCP/UDP/UDPLite communication.

## tools
Generation scripts for easier building

## buildroot
*in progress*

Contains buildroot configuration for both flight and ground targets.

# Results

This video was recorded on 03-feb-2017 during an FPV session
[![FPV Video of drone](http://img4.hostingpics.net/pics/934968drone20170302.gif)](https://drive.google.com/file/d/0Bwo1JutVEkplLV9DNU5hZEtFcnM/view?usp=sharing)

# Current test setup

![Photo of drone and controller](http://img4.hostingpics.net/pics/982150drichdronezmrcontrollersmall.jpg)

Racer : 
 
* ZMR250 frame
* Raspberry Pi 3
* Raspberry Pi official camera v2.1 (8MP, Sony IMX219 sensor) + wide-angle lens
* Alfa Network AWUS036NH (maxed out to 2000mW TX power)
* MPU9150 (gyroscope + accelerometer + magnetometer)
* ADS1015 (ADC/Voltmeter)
* ACS709 (current sensor)
* 4x DYS XM20A ESCs (flashed with BLHeli, DampedLight enabled)
* 4x Multistar Elite 2204-2300KV motors

RC controller : 
 
* Taranis X9D empty case
* Taranis X9D gimballs
* Raspberry Pi 2
* Waveshare 3.5" touchscreen
* Alfa Network AWUS036NH (maxed out to 2000mW TX power)
* MCP3208 ADC (for gimballs input and battery voltage sensing)
* 10x ON-ON switches
