```
============================================================
The vgaterm peripherals provide a text interface to the
host using a VGA display.  The font is the original Codepage
734 which from IBM.


HARDWARE
The vgaterm uses 8 FPGA pins to provide two bits for each
of the RGB colors and one bit for each of horizontal sync
and vertical sync.  This peripheral is primarily intended
for the Digilent Basys3 FPGA board but can work with any
FPGA with the appropriate VGA connector.


RESOURCES
char : a FIFO for characters to the display on write, and
a read of the character under the cursor on read.  Up to
80 characters can be written at once.  A read returns not
only the character but its attributes includeing foreground
and background colors and whether or not it is underlined
and blinking.

cursor : sets the cursor location as well as the style of
the cursor.  The location is specified as row and column
where row in the range of 1 to 40 and column is in the range
of 1 to 80.  The attributes for cursor specify whether the
cursor is an underline or a block, and whether or not it is
visible.
  Specify cursor style as 'b' or 'u' for block or underline.
Specify cursor visibility as 'v' or 'i' for visible or
invisible.

attr : sets the foreground and background colors as well as
whether or not characters are underlined or blinking.  The
attributes are applied only as new characters are added to
the buffer.
  Colors are 6 bit hex numbers as rrggbb.  White is 3f,
black is 00, red is 30, and green is 0c.
Underline is specifiec at 'u' or 'n' and blink is specified
at 'b' or 'n'.

rowoff : Rowoff adds an offset to the row to be displayed
at the top of the screen.  Setting rowoff to 1 causes the
entire display to 'scroll up' one line.  A common usage
is implement line feed as adding 1 to the rowoff.


EXAMPLES
  # characters should be green on a black background without
  # underline.  Position the cursor at the top left of the
  # display and show ' Hello, World! '
  pcset vgaterm attr 0c 00 n n
  pcset vgaterm cursor 1 1 b v
  pcset vgaterm char  ' Hello, World! '


```
