/*
 * Name: pktsend.c
 * 
 * Description: Send packet on command line to the serial port
 *
 * Invocation: ./pktsend /dev/ttyUSB1 f6 e0 40 20 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>              // for PATH_MAX
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
#define PC_PKTLEN       514     /* PeriCtrl protocol packet size */
#define PKT_DATA_SZ     510     // Max # bytes in packet payload
        // SLIP Protocol characters
#define SLIP_END      ((unsigned char) 192)
#define SLIP_ESC      ((unsigned char) 219)
#define INPKT_END     ((unsigned char) 220)
#define INPKT_ESC     ((unsigned char) 221)





/***************************************************************************
 *  - Function prototypes
 ***************************************************************************/
void         openfpgaserial(char *);
int          pktsend(uint8_t *inpkt, int len);
int          pctoslip(uint8_t *, int, uint8_t *);
uint16_t     crc16(uint8_t *, int);


/***************************************************************************
 *  - System-wide global variable allocation
 ***************************************************************************/
int      fpgaFD;               // -1 or fd to SerialPort



/***************************************************************************
 *  - main():
 ***************************************************************************/
int main(int argc, char *argv[])
{
    uint8_t  data[PC_PKTLEN];  // assemble byte from command line here
    unsigned int  hexval;
    int      i;

    // Open serial port to the FPGA
    if ((argc >= 6) && (argc < PC_PKTLEN)) {
        openfpgaserial(argv[1]);
    }
    else {
        printf("pktsend requires the serial port and at least four arguments\n");
        exit(-1);
    }

    // Collect bytes to send from the command line
    for (i = 2; i < argc; i++) {
        if (1 != sscanf(argv[i], "%x", &hexval)) {
            printf("Failed to convert %s to hexadecimal\n",argv[i]);
            exit(-1);
        }
        data[i - 2] = hexval;
    }

    (void) pktsend(data, argc - 2);

    return (0);
}





/***************************************************************************
 *  Open serial port to the FPGA
 ***************************************************************************/
void openfpgaserial(char *serialport)
{
    struct termios tbuf;       // set baud rate
    int      actions;

    fpgaFD = open(serialport, (O_WRONLY), 0);
    if (fpgaFD < 0) {
        printf("Unable to open '%s' for reading\n", serialport);
        exit(-1);
    }

    // port is open and can be configured
    tbuf.c_cflag = CS8 | CLOCAL | DEFBAUD;
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
 *  pktsend():  Send a packet to the board
 *     Return number of bytes sent or -1 on error
 ***************************************************************************/
int pktsend(
    uint8_t *inpkt,    // The packet to send
    int      len)      // Number of bytes in the packet
{
    uint8_t  sltx[PC_PKTLEN]; // SLIP encoded packet
    int      txcount;  // Length of SLIP encoded packet
    int      sntcount; // Number of bytes actually sent
    int      i;

    // First byte is command, second is slot
    inpkt[0] = inpkt[0] | 0xf0;  // helps error checking
    inpkt[1] = inpkt[1] | 0xe0;

    // Convert PC pkt to a SLIP encoded packet
    txcount = pctoslip(inpkt, len, sltx);

    // write SLIP packet to the USB FD
    sntcount = write(fpgaFD, sltx, txcount);
    if (sntcount != txcount) {
        printf("packet write failed\n");
        exit(-1);
    }
    printf(">> ");
    for (i = 0; i < txcount; i++)
        printf("%02x ", sltx[i]);
    printf("\n");

    return (0);
}


/***************************************************************************
 *  pctoslip():  Convert a PC packet to a SLIP encoded PC packet
 *  Return the number of bytes in the new packet
 ***************************************************************************/
int pctoslip(
    uint8_t *pcpkt,        // The unencode PC packet (input)
    int      len,          // Number of bytes in pcpkt
    uint8_t *slppkt)       // The SLIP encoded packet (output)
{
    int      dpix = 0; // Index into the input PC packet
    int      slix = 0; // Indes into the output SLIP packet
    uint16_t crc;      // CRC16/XMODEM

    // Sanity check on input length
    if (len > PC_PKTLEN)
        return (0);

    // Computer the CRC and add it to the end of the packet
    crc = crc16(pcpkt, len);
    pcpkt[len] = (crc >> 8);
    pcpkt[len +1] = crc & 0x00ff;

    // Van Jacobson encoding.  Send opening SLIP_END character
    slppkt[slix++] = SLIP_END;

    // Copy the input packet to the output packet but replace any
    // ESC or END characters with their two character equivalent
    for (dpix = 0; dpix < len+2; dpix++) {   // +2 for crc bytes
        if (pcpkt[dpix] == SLIP_END) {
            slppkt[slix++] = SLIP_ESC;
            slppkt[slix++] = INPKT_END;
        }
        else if (pcpkt[dpix] == SLIP_ESC) {
            slppkt[slix++] = SLIP_ESC;
            slppkt[slix++] = INPKT_ESC;
        }
        else {
            slppkt[slix++] = pcpkt[dpix];
        }
    }

    // Send closing SLIP_END character
    slppkt[slix++] = SLIP_END;

    return (slix);
}


// crc16/xmodem.
uint16_t crc16(uint8_t *pkt, int length)
{
    uint8_t   c;
    uint8_t   x;
    uint16_t  crc = 0x0000;

    while (length --) {
        c = *pkt;
        //c = *pkt++;
        x = (crc >> 8) ^ c;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t)x << 12) ^ ((uint16_t)x << 5) ^ ((uint16_t)x);
        pkt++;
    }
    return (crc);
}

