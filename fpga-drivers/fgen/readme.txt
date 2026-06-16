```

HARDWARE
  The fgen function generator provide sine, triangle, and
square wave outputs with 8 bits of resolution.  It uses
8 FPGA pins and is usually tied to an 8 bit R-2R DAC.
The phase accumulator is 32 bits and the update frequency
is 100 MHz giving it a useful frequency range of about 
2 MHz to 0.04 Hz.

   The peripheral uses one phase update value for the 
first half of the cycle and a different value for the
second half.  This lets triangle mode output both rising
and falling ramps, and a square wave can be a pulse.


RESOURCES
You can specify the waveform, frequency and symmetry
using the config resource.

config:
   The config resource sets the frequency, duty cycle, and
type of the output.  Frequency is in Hertz and must be in
the range of 2000000 to 0.04.  Symmetry is a percent in the
range of 0 to 100 percent.  Values of 0 and 100 are useful
to force a sharp transition at the end of the half cycle.  
Type is one of 'off', 'sine', 'triangle', or 'square'.

Examples:
    # A 1 KHz square wave with 50 percent duty cycle.
    pcset fgen config 1000 50 square
    # A rising 10 KHz ramp
    pcset fgen config 10000 100 triangle


```
