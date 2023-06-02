```

HARDWARE
   The Peripheral Control ps2 peripheral provides an interface
between a PS/2 keyboard or mouse to a host.  The PS/2 data
line goes to pin 1 of the FPGA port and the clock goes to pin
3.  Pins 2 and 4 are ignored.


RESOURCES
   data : Bytes from the PS/2 device and commands to it.
Keyboard scancodes and mouse packet are displayed using pccat
at space separated two-digit hex numbers.  Commands to the
device are specified as single hex numbers.  This resource
works with pccat and pcset.


EXAMPLES
   To monitor a PS/2 keyboard for scancodes:
      pccat ps2 data

   To set all keyboard LEDs on
      pcset ps2 data ed     # set LEDs
      pcset ps2 data 7      # LEDs in three LSBs

   To enable a mouse and listen for mouse movements
      pcset ps2 data f4     # enable scanning
      pccat ps2 data


```
