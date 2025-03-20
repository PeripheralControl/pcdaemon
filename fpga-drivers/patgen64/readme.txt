```
INTRODUCTION
    The four FPGA pins are controlled by a 64x4 RAM
that is set by the host.  The outputs continuously
cycle at the selected frequency and can use less then
all 64 locations.  


HARDWARE
    The RAM outputs directly control the four FPGA pins.
Applications for the patgen64 include LEDs and simple
digital to analog converters.


RESOURCES
pattern : Up to sixty four hex values that describe the
output pattern.  Each hex value controls the outputs for
that time slot.  Non hex characters are quietly ignored
and patterns with more then 64 values use just the first
64.  Patterns may have less than 64 values but you should
set the pattern length to match.

frequency : Sets the pattern clock rate.  Specified in 
Hertz with values between 5 and 20000000.  Input frequencies
not in this list are quietly rounded to the next lower value.
The frequency should be one of the following:
  20000000 -- 20 MHz
  10000000 -- 10 MHz
   5000000 -- 5 MHz
   1000000 -- 1 MHz
    500000 -- 500 KHz
    100000 -- 100 KHz
     50000 --  50 KHz
     10000 --  10 KHz
      5000 --   5 KHz
      1000 --   1 KHz
       500 -- 500 Hz
       100 -- 100 Hz
        50 --  50 Hz
        10 --  10 Hz
         5 --   5 Hz
         0 --  Off

length : The length of the pattern before it repeats.
The repeat length must be between 1 and 64.


EXAMPLES
pcset patgen64 pattern 00ff00ff0000005500ff00ff0000005500ff00ff0000005500ff00ff00000055
pcset patgen64 pattern 00ff00ff00000055
pcset patgen64 pattern 01248.01248
pcset patgen64 length 10
pcset patgen64 frequency 100


```
