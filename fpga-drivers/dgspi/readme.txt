```

HARDWARE
   This peripheral provides a generic SPI interface with
Chip Select on pin 1, MOSI on pin 2, MISO on pin 3, and
SCK on pin4.


RESOURCES
The host interface to the SPI peripheral let you specify
SCLK clock frequency, the behavior of the chip select line,
the polarity of SCK line, and the data to and from the device.
You may also have the FPGA repeat the last packe you sent at
a multiple of 0.01 seconds.

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
  pcset dgspi config 1000000  0 al

data:
    Sending an SPI packet acts as both input and as output.
The pcget command provides both read and write functionality.
When reading data be sure to supply enough bytes inthe outgoing
packet for the return data.  The data to send is a single line
of up to 63 space-separated hexadecimal numbers.  The returned
data is also a  single line of space separated hexadecimal
numbers.  For example:
    pcget dgspi data 12 34 56 78
might return
    00 33 00 10

polltime
    The FPGA can automatically replay the last packet sent and
send the returned data to the host.  The polltime resource set
the interval of these automatic packet.  It is specified in
units of 0.01 seconds and can range from 0 (off) to 250 (2.5
seconds).

polldata
    Data from an automatic packet replay is made available on
the polldata resource using the pccat command.  The format is
the same as the pcget response.  Polldata only works with the
pccat command.


EXAMPLES:
    Configure a MAX31855 thermocouple sensor for 2 megabits
per second, active low chip select and data valid on the
rising edge of SCK.  Read the temperature once and then set 
up a poll timer to read the temperature every 2 seconds.  Use
pccat to collect the automatic poll data.

    pcset dgspi config 2000000 0 al
    pcget dgspi data 00 00 00 00    # no real data to send
    pcset dgspi polltime 200
    pccat dgspi polldata

```
