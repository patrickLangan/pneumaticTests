#Single Cylinder Pneumatic Tests
==============

##Wiring
	spi-0
		Clock:  P9_22
		Data0:  P9_21 (MISO)
	spi-1
		Clock:  P9_31
		Data0:  P9_29 (MISO)

	i2c-1
		Clock:  P9_19
		Data:   P9_20

	i2c-2
		Clock:  P9_17
		Data:   P9_18

	//Note: cannot enable both i2c-1 and spi-0

	Length sensor - PRU1
		Clock:		P8_11
		Sensor1Data:	P8_15
		Sensor2Data:	P8_16

##TODO
- Accelerometer
- Flow sensor
- Temperature sensor
- Stepper valves
- Directional control valve
- Calibration
- Error checking
- General cleanup

