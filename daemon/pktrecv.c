/*
 * Name: pktrecv.c
 * 
 * Description: Displays PeriCtrl packet received from the specified serial port.
 * 
 * Invocation:  ./pktrecv /dev/ttyUSB1
 *
 */

/*
 * 
 * pktrecv flow:
 *   -- Process command line options
 *   -- Init
 *   -- Main loop
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <termios.h>
#include <sys/ioctl.h> 
#include <linux/serial.h>

/***************************************************************************
 *  - Limits and defines
 ***************************************************************************/
#define DEFBAUD         B115200
        // give up after trying to reset the FPGA this many times
#define MAXFPGARESET      100
        // SLIP Protocol characters
#define SLIP_END      ((unsigned char) 192)
#define SLIP_ESC      ((unsigned char) 219)
#define INPKT_END     ((unsigned char) 220)
#define INPKT_ESC     ((unsigned char) 221)
        // SLIP decoder states
#define SKIP_FIRST_ZEROES (0)
#define AWAITING_PKT      (1)
#define IN_PACKET         (2)
#define INESCAPE          (3)
        // receive buffer size
#define RXBUF_SZ       (4096)

/***************************************************************************
 *  - Function prototypes
 ***************************************************************************/
void openfpgaserial(char *);
void  pktrecv();


/***************************************************************************
 *  - System-wide global variable allocation
 ***************************************************************************/
int      fpgaFD = -1;                  // -1 or fd to serial port
unsigned char rxBuf[RXBUF_SZ];         // Serial port character buffer
int      rxIdx = 0;                    // index into rxBuf
int      slState = SKIP_FIRST_ZEROES;  // current state of the decoder


/***************************************************************************
 *  - main():
 ***************************************************************************/
int main(int argc, char *argv[])
{

    // Open serial port to the FPGA
    if (argc == 2) {
        openfpgaserial(argv[1]);
    }
    else {
        printf("pktrecv requires the serial port as an argument\n");
        exit(-1);
    }

    // Drop into a loop and wait for characters
    while (1) {
        pktrecv();
    }

    return (0);
}



/***************************************************************************
 *  Open serial port to the FPGA
 ***************************************************************************/
void openfpgaserial(char *serialport)
{
    struct termios tbuf;       // set baud rate
    int      actions;

    fpgaFD = open(serialport, (O_RDONLY), 0);
    if (fpgaFD < 0) {
        printf("Unable to open '%s' for reading\n", serialport);
        exit(-1);
    }

    // port is open and can be configured
    tbuf.c_cflag = CS8 | CREAD | CLOCAL | DEFBAUD;
    tbuf.c_iflag = IGNBRK;
    tbuf.c_oflag = 0;
    tbuf.c_lflag = 0;
    tbuf.c_cc[VMIN] = 1;        /* character-by-character input */
    tbuf.c_cc[VTIME] = 0;       /* no delay waiting for characters */
    actions = TCSANOW;
    if (tcsetattr(fpgaFD, actions, &tbuf) < 0) {
        printf("Unable to set port baud rate\n");
        exit(-1);
    }

    return;
}


/***************************************************************************
 *  Listen for packet and display them on stdout
 ***************************************************************************/
void pktrecv()
{
    unsigned char pcpkt[RXBUF_SZ]; // the SLIP decoded packet
    int      dpix;                 // index into pcpkt
    unsigned char c;               // current char to decode
    int      rdret;                // read return value
    int      i,j;                  // buffer loop counters

    rdret = read(fpgaFD, &(rxBuf[rxIdx]), (RXBUF_SZ - rxIdx));

    // Was there an error or has the port closed on us?
    if (rdret <= 0) {
        if ((errno != EAGAIN) || (rdret == 0)) {
            printf("Fatal read() error on serial port\n");
            exit(-1);
        }
        // EAGAIN means it's recoverable and we just try again later
        return;
    }
    rxIdx += rdret;


    // At this point we have read some bytes from the host port.  We
    // now scan those bytes looking for SLIP packets.  We put any
    // packets we find into the pcpkt buffer and then dispatch the
    // completed packet to the packet handler.
    // Packets with a protocol violation are dropped with a log
    // message.
    // It sometimes happens that a read() returns a full packet and
    // a partial packet.  In this case we process the full packet and
    // move the bytes of the partial packet to the start of the buffer.
    // This way we can always start the SLIP processing at the start 
    // of the buffer.  
    // Now drop into a loop to process all the packets in the buffer
    dpix = 0;               // at start of a new decoded packet

    for (i = 0; i < rxIdx; i++) {
        c = rxBuf[i];

        if (c == SLIP_END) {
            if (slState == IN_PACKET) {
                // Process completed packet and set up for next one
                if (dpix > 0) {
                    // Do processing on packet here
                    //dispatch_packet(pcpkt, dpix);
                    printf("<< ");
                    for (j=0; j<dpix; j++)
                        printf("%02x ", pcpkt[j]);
                    printf("\n");
                }
                // return if no more bytes in buffer
                if (i == rxIdx) {
                    rxIdx = 0;
                    return;
                }
                // else move remaining bytes in buffer down and scan again
                (void) memmove(rxBuf, rxBuf  + i , rxIdx - i );
                rxIdx = rxIdx - i; //- 1;
                slState = IN_PACKET;
                dpix = 0;
                i = 0;      // scan again from start of buffer
            }
        }
        else if (c == SLIP_ESC) {
            // this should only occur while IN_PACKET
            if (slState == IN_PACKET)
                slState = INESCAPE;
            else {
                // A protocol error.  Report it. Move remaining bytes down
                printf("SLIP protocol error\n");
                (void) memmove(rxBuf, rxBuf + i , rxIdx - i);
                rxIdx = rxIdx - i - 1;
                slState = IN_PACKET;
                dpix = 0;
                i = 0;
            }
        }
        else if ((c == INPKT_END) && (slState == INESCAPE)) {
            pcpkt[dpix] = SLIP_END;
            dpix++;
            slState = IN_PACKET;
        }
        else if ((c == INPKT_ESC) && (slState == INESCAPE)) {
            pcpkt[dpix] = SLIP_ESC;
            dpix++;
            slState = IN_PACKET;
        }
        else if ((c == 0x00) && (slState == SKIP_FIRST_ZEROES)) {
            /* ignore zero byte outside of packet */
            printf("skipping zero byte\n");
            slState = SKIP_FIRST_ZEROES; // keep skipping zeroes
            continue; // ignore byte
        }
        else if ((c != 0x00) && (slState == SKIP_FIRST_ZEROES)) {
            /* first char not zero in stream we need it */
            /* if empty start frame ignore it */
            if (c == SLIP_END) {
                printf("skipping empty frame\n");
                slState = IN_PACKET;
                continue;
            } else {
                // printf("first byte %x\n",c);
                // valid data, first found in the stream, add to buffer
                pcpkt[dpix] = c;
                dpix++;
                slState = IN_PACKET; // we are getting packets from now on
            }
        }
        else {
            // a normal character
            pcpkt[dpix] = c;
            dpix++;
        }
    }
}



