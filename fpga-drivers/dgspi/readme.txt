```

HARDWARE
   This peripheral provides a generic SPI interface with
Chip Select on pin 1, MOSI on pin 2, MISO on pin 3, and
SCK on pin4.


RESOURCES
The device interfaces to the SPI peripheral let you
specify SCLK clock frequency, the behavior of the chip
select line, the polarity of SCK line, and the data to
and from the device.

config:
   The configuration for the DGSPI port is entered into and
available from the config resource.  The configuration is
entered as three strings separated by space and terminated
by a new line.
   The meaning of the strings is given below.

CLOCK FREQUENCY
   The frequency of the sck signal in Hertz - the driver
will round the input frequency down to the next available
clock frequency.  Available clock frequencies are:
   2000000 -- 2 MHz
   1000000 -- 1 MHz
    500000 -- 500 KHz
    100000 -- 100 KHz

SCK POLARITY
  A '0' value for MOSI valid on the rising edge of SCK and
a '1' value for MOSI valid on the falling edge.

CHIP SELECT MODE
   Specifies the behavior of the chip select line.  The
following are the possible choices:
  'al'  -- active low: low during transfers, high otherwise
  'ah'  -- active high: high during transfers, low otherwise
  'fl'  -- forced low
  'fh'  -- forced high

A sample configuration line for a SPI port using SPI Mode 0
at 1 MHz, and with an active low chip select could be entered
into the device node as follows:
  pcset espi config 1000000  0 al
  
data:
    Due to the nature of SPI, pcget provides both read and write
functionality. Each read requires a write first, and bytes must
be provided to fill with resulting data.
    The data must be a single line of up to 63 space-separated
hexadecimal numbers, corresponding to one packet of data. 
    Returns the data read from the SPI peripheral. The returned
data  is a single line of space separated hexadecimal numbers.

Example:
    pcget dgspi data 12 34 56 78

Returns:
    00 00 00 10

```
