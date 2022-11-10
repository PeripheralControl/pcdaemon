```
============================================================

HARDWARE
   The cvcc card has electronis that convert PWM signals from
the FPGA into analog voltage levels that set the maximum
current and maximum voltage.  The cvcc card also measures
the load current and voltage and reports these as PWM signals
to the FPGA.  
   The cvcc card is documented in the PeripheralControl
boards repository and is copyright Jerry O'Keefe, 2022.  It
is released under a Creative Commons License.


RESOURCES
viout : The voltage and current setting as a percent of full
scale.  Set either or both of to zero to disable the power
supply.  This resource works with pcget and pcset.  The
default values are zero and zero.

viin : The voltage and current at the load as a percent of
full scale. The resource works with pccat and pcget.  When
used with pccat the update rate is about 50Hz.

conf : The frequency of the clock driving the output FET 
and divide constant that generates the input PWM rates.
Frequency is given in Hertz and is rounded to the nearest
whole multiple of 20 MHz.  The divide constant must be a
multiple of 2 in the range of 2 to 64.  This resource works
with pcget and pcset.  The default values are 1 MHz and 16.


EXAMPLES
# Turn off the outputs
pcset cvcc viout 0 0
# Set the frequency to 1 MHz and the divider to 16
pcset cvcc conf 1000000 16
# Set the the voltage limit to # 50 percent of full scale and
# current limit to maximum.  This is constant voltage mode.
pcset cvcc viout 50 100
# Start a stream of load voltage and current reading
pccat cvcc viin

```
