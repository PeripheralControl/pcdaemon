```
============================================================

runber

HARDWARE
   The Seeed Studio Runber board has eight buttons, eight switches,
four RGB LEDs, and four seven-segment displays.  This is a board
driver and so includes the list of driver IDs corresponding to the
list of peripherals in the FPGA image.
   The host interface is Tx/Rx with Tx on J2 pin 2 and Rx on J2 
pin 3.


RESOURCES
drivlist : list of driver IDs
   Each of the peripherals in the FPGA image has a recommended 
driver identification number.  Use pcget on this resource to see
the list of up to sixteen driver IDs in this build.  For example:
        pcget runber drivlist

switches : state of buttons and switches on the board
   Use pcget or pccat to get the values of the switches and the
buttons on the board.  The return value is two 8-bit hex numbers.
with the buttons as the first hex value and the switches as the
second hex value  For example:
        pcget runber switches
        pccat 0 switches

rgb: state of the four RGB LEDs
   Use pcset to control the on/off state of the twelve LEDs (RGB
for each of four LED).  Specify the state using three hex digits
where the first hex digit controls the red LEDs, the second hex
digit controls the green LEDs, and the third digit controls the
blue LEDs.  For example:
        pcset runber rgb fff        # All LEDs on
        pcset runber rgb f00        # All red LEDs on
        pcset runber rgb 00f        # All blue LEDs on
 
display : state of the four 7-segment displays
   Use pcset to display four characters on the 7-segment display.
Characters for the text message must be taken from the following set:
        0 1 2 3 4 5 6 7 8 9
        A b C d E F  (may be given as upper or lower case)
        o L r h H - u
        (space) (underscore) (decimal point)
The characters you enter are converted to eight segment values
which are visible in the segments resource described below.  Examples
of use include:
        pcset 0 display 8.8.8.8.     # All segments on
        pcset 0 display '    '       # Four spaces = all segments off
        pcset 0 FEED                 # a four digit hex value

segments : individual segment control
   You can directly control which segments are displayed by writing
four space-separated hexadecimal values to the segments resource.
The LSB of each value controls the 'a' segment and the next-MSB value
controls the 'b' segment.  The MSB controls the decimal point.  For
example:
        pcset 0 segments 1 1 1 1     # All 'a' segments on
        pcset 0 segments 40 00 40 00 # Show '- - '

```
