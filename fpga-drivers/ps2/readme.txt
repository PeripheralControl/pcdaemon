```

HARDWARE
   The Peripheral Control ps2 peripheral provides an interface
between a PS/2 keyboard or mouse to a host.  The PS/2 data
line goes to pin 1 of the FPGA port and the clock goes to pin
3.


RESOURCES
   data :
   There are no configuration options for the PS/2 interface.
The only resource is 'data' which displays the received bytes
as two digit hexadecimal numbers.  Data works with the pccat
command.


EXAMPLES
   To monitor a PS/2 keyboard for scancodes:
      pccat ps2 data

```
