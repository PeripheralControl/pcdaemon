```
============================================================

HARDWARE
The octal RC analog to digital Converter (rcc) provides eight
channels of analog input by measuring the discharge time of a
resistor capacitor circuit.  This circuit is found in the Pololu
QTR-RC for example.  Both charge and discharge circuits are
supported by specifying the polarity of the initial charge.

RESOURCES
The rcc interface lets you select the polarity of the initial
charge, the time base for the measurement, and the user update
interval.

rccval : Eight two digit hex numbers followed by a newline.
The value is the number of clock ticks from the initial charge
to the 1->0 transition.  For polarity 0 the time is the number
of tick waiting for the 0->1 transition.  A higher number
indicates a longer time and so a lower discharge rate.

config : The polarity, clock source, and update interval.
 polarity : Polarity specified the initial charge applied to
pin.  A value of 0 applies logic zero as the initial charge.
 clock source : Valid tick frequencies are 1 MHz, 100 KHz,
10 KHz, and 1 KHz.  Chose the value based on the RC time
constant in your circuit.
 update_period : Update period for the rccval resource in tens
of milliseconds.  That is, the pccat command and select() on
rccval will give a readable file descriptor every update
milliseconds.  Setting this to zero turns off all output from
the sensor.  The update period must be between 0 and 150
milliseconds in steps of 10 milliseconds.   That is, valid
values are 0, 10, 20, 30, 40, ... 140, or 150.


EXAMPLES
Set the polarity 1, the clock source to 100 KHz, and the sample
rate to 50 milliseconds:

  pcset rcc8 config 1 100000 50
  pccat rcc8 rccval


```
