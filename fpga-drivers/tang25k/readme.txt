```
============================================================

HARDWARE
    The Tang Primer 25K has three 8 pin Pmod connectors.
    The tang25k peripheral is a 'board peripheral' and so has
the list of driver IDs for the peripherals in the FPGA build.


RESOURCES
driverlist : This is a read-only resource that returns
the identification numbers of the drivers requested for
the peripherals in the FPGA build.  It works only with
pcget and returns sixteen space separated hex values.


EXAMPLES
   pcget tang25k driverlist


```
