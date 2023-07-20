============================================================

HARDWARE
    Each Digilent CmodS7 board has four red LEDs, one RGB
LED, and two push buttons.  The board comes in 48-bin wide
DIP format with 36 pins populated.  The manual is available
here:
https://digilent.com/reference/programmable-logic/cmod-s7/reference-manual

    The LEDs are tied internally to the pins of Peripheral
Control Slot #1.  This provides a convenient way to monitor
this port.

    The cmods7 peripheral is a 'board peripheral' and so has
the list of driver IDs that the daemon loads at daemon
start time.  



RESOURCES

buttons : Value of the buttons as one hex digit in the range
of 0 to 3.  This resource works with pcget and pccat.  Push
button #0 is the LSB.

rgb : RGB LED control.
   The RGB LED is controlled by pcset where valid values are
hex digits in the range of 0 to 7.  The MSB (bit 2) controls
the red LED, bit 1 controls the green LED, and bit 0 controls
the blue LED.

driverlist : This is a read-only resource that returns the
identification numbers of the drivers requested for the
peripherals in the FPGA build.  It works only with pcget and
returns sixteen space separated hex values.


EXAMPLES
    pcget cmods7 driverlist
    pcset cmods7 rgb 1   # just the blue LED on
    pcset cmods7 rgb 7   # all LEDs on
    pcset cmods7 rgb 0   # all LEDs off
    pccat cmods7 buttons


