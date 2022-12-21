```
============================================================

ISL29125 RGB Sensor 
This module provides a simple ASCII interface to the ISL29125
color sensor.  You can set update rate from 50 ms to 5000 ms
in steps of 50 ms.  An update rate of 0 disable polling of
the sensor.

NOTE: Only one ISL29125 plug-in can be loaded.  Loading
more than one instance of this peripheral will result 
in unexpected results.


RESOURCES

bus : The I2C bus number as a decimal number.  The default
of 0 gives an I2C bus device of /dev/i2c-0.  

period : The period in milliseconds in steps of 50 ms.  A
value of 0 disable the sensor.  The maximum value is 5000 ms.

colors : A broadcast resource that give RGB data as three
16-bit hex numbers followed by a newline.  The numbers are
not calibrated to a specific standard and should be used only
for relative color intensity.  The order of the number is
red, green, blue.


EXAMPLE
  Set the device to I2C bus number 0, the update rate to 100
milliseconds, and start a stream of measurements:

    pcset isl29125 bus 0
    pcset isl29125 period 100
    pccat isl29125 colors


```
