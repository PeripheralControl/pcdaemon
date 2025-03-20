/*
 *  Name: patgen64.c
 *
 *  Description: Driver a 64x4 pattern generator
 *
 *  Registers: (8 bit)
 *      Reg 0:  State of output pins in state 0
 *      Reg 1:  State of output pins in state 1
 *      Reg 2:  State of output pins in state 2
 *      Reg 3:  State of output pins in state 3
 *      :::::::::::::::::::::::::::::::::::::::::
 *      Reg 62: State of output pins in state 62
 *      Reg 63: State of output pins in state 63
 *      Reg 64: Clock source
 *      Reg 65: Repeat length-1 (zero to 63)
 *
 *      This peripheral is a RAM based 4 bit wide by 'length' pattern
 *  generator where length is between 1 and 64.  The clock driving the
 *  sequence counter can be set by the user to one of the following:
 *      0:  Off
 *      1:  20 MHz
 *      2:  10 MHz
 *      3:  5 MHz
 *      4:  1 MHz
 *      5:  500 KHz
 *      6:  100 KHz
 *      7:  50 KHz
 *      8:  10 KHz
 *      9   5 KHz
 *     10   1 KHz
 *     11:  500 Hz
 *     12:  100 Hz
 *     13:  50 Hz
 *     14:  10 Hz
 *     15:  5 Hz
 *
 *
 * Copyright:   Copyright (C) 2014-2025 Demand Peripherals, Inc.
 *
 * License:     This program is free software; you can redistribute it and/or
 *              modify it under the terms of the Version 2 of the GNU General
 *              Public License as published by the Free Software Foundation.
 *              GPL2.txt in the top level directory is a copy of this license.
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *              GNU General Public License for more details. 
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include "daemon.h"
#include "readme.h"

/**************************************************************
 *  - Limits and defines
 **************************************************************/
        // Pattern generator register definitions
#define PG64_PATTERN        0x00
#define PG64_FREQ           0x40
#define PG64_LENGTH         0x41
#define FN_PATTERN          "pattern"
#define FN_FREQ             "frequency"
#define FN_LENGTH           "length"
        // Resource index numbers
#define RSC_PATTERN         0
#define RSC_FREQ            1
#define RSC_LENGTH          2
        // Max data payload size
#define MXDAT               64


/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of a patgen64
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    void    *ptimer;   // timer to watch for dropped ACK packets
    int      freq;     // clock frequency in Hertz
    int      length;   // length of pattern in range 1 to 64
    char     pattern[MXDAT];  // pattern as hex characters
} PG64DEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void userpattern(int, int, char*, SLOT*, int, int*, char*);
static void userfrequency(int, int, char*, SLOT*, int, int*, char*);
static void userlength(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, PG64DEV *);
static int  pg64tofpga(PG64DEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    PG64DEV *pctx;     // our local device context
    int      i;        // loop index

    // Allocate memory for this peripheral
    pctx = (PG64DEV *) malloc(sizeof(PG64DEV));
    if (pctx == (PG64DEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in patgen64 initialization");
        return (-1);
    }

    // Init our PG64DEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response
    pctx->freq = 0;            // Zero turn off the clock
    pctx->length = MXDAT;      // Up to 64 nibbles in pattern
    for (i = 0; i < MXDAT; i++) {
        pctx->pattern[i] = '0';
    }

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_PATTERN].name = FN_PATTERN;
    pslot->rsc[RSC_PATTERN].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_PATTERN].bkey = 0;
    pslot->rsc[RSC_PATTERN].pgscb = userpattern;
    pslot->rsc[RSC_PATTERN].uilock = -1;
    pslot->rsc[RSC_PATTERN].slot = pslot;
    pslot->rsc[RSC_FREQ].name = FN_FREQ;
    pslot->rsc[RSC_FREQ].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_FREQ].bkey = 0;
    pslot->rsc[RSC_FREQ].pgscb = userfrequency;
    pslot->rsc[RSC_FREQ].uilock = -1;
    pslot->rsc[RSC_FREQ].slot = pslot;
    pslot->rsc[RSC_LENGTH].name = FN_LENGTH;
    pslot->rsc[RSC_LENGTH].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_LENGTH].bkey = 0;
    pslot->rsc[RSC_LENGTH].pgscb = userlength;
    pslot->rsc[RSC_LENGTH].uilock = -1;
    pslot->rsc[RSC_LENGTH].slot = pslot;
    pslot->name = "patgen64";
    pslot->desc = "64x4 Pattern Generator";
    pslot->help = README;

    return (0);
}

/**************************************************************
 * packet_hdlr():  - Handle incoming packets from the FPGA board
 **************************************************************/
static void packet_hdlr(
    SLOT   *pslot,     // handle for our slot's internal info
    PC_PKT *pkt,       // the received packet
    int     len)       // number of bytes in the received packet
{
    PG64DEV *pctx;    // our local info

    pctx = (PG64DEV *)(pslot->priv);  // Our "private" data is a PG64DEV

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  //Got the ACK
            pctx->ptimer = 0;
        }
    }
    else {
        // Only valid packet is a write response
        pclog("invalid patgen64 packet from board to host");
    }

    return;
}


/**************************************************************
 * userpattern():  - The user is setting or getting the 64x4
 * pattern as a string of 64 hex digits.
 **************************************************************/
static void userpattern(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    PG64DEV *pctx;     // our local info
    int      ret;      // return count
    int      len;      // length of hex string in input
    int      hidx;     // index into array of hex values 
    int      i;        // index into input string
    int      txret;    // ==0 if the packet went out OK

    pctx = (PG64DEV *) pslot->priv;


    if (cmd == PCGET) {
        // Quietly error out if not enough buffer space
        if (*plen < (MXDAT + 1)) {
            *plen = 0;
            return;
        }
        for (hidx = 0; hidx < MXDAT; hidx++) {
            buf[hidx] = pctx->pattern[hidx];
        }
        buf[hidx++] = '\n';
        *plen = hidx;
        return;
    }

    // User is setting the pattern.  Format of pattern is something
    // like abcf03 and can be up to 64 hex characters long.  Copy
    // string, ignore spaces, and validate other char are hex.
    len = strlen(val);
    hidx = 0;
    for (i = 0; i < len; i++) {
        // No error if input string is more than 64 char
        if (hidx == MXDAT) {
            break;
        }
        // ignore non hex characters
        if (isxdigit(val[i])) {
            pctx->pattern[hidx++] = val[i];
        }
    }

    txret =  pg64tofpga(pctx);   // This peripheral's context
    if (txret != 0) {
        // the send of the RGB data did not succeed.  This probably
        // means the input buffer to the USB port is full.  Tell the
        // user of the problem.
        ret = snprintf(buf, *plen, E_WRFPGA);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // Start timer to look for a write response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    return;
}


/**************************************************************
 * userfrequency():  - The user is setting or getting the output
 * frequency of the pattern generator
 **************************************************************/
static void userfrequency(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    PG64DEV *pctx;     // our local info
    int      ret;      // return count
    int      newfreq;  // frequency from input command
    int      txret;    // ==0 if the packet went out OK

    pctx = (PG64DEV *) pslot->priv;

    if (cmd == PCGET) {
        ret = snprintf(buf, *plen, "%d\n", pctx->freq);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // User is setting the frequency
    ret = sscanf(val, "%d", &newfreq);
    if (ret != 1) {
        ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
        *plen = ret;
        return;
    }
    // No errors on bad freq, just round down.
    pctx->freq = 
         (newfreq >= 20000000) ? 20000000 :
         (newfreq >= 10000000) ? 10000000 :
         (newfreq >=  5000000) ?  5000000 :
         (newfreq >=  1000000) ?  1000000 :
         (newfreq >=   500000) ?   500000 :
         (newfreq >=   100000) ?   100000 :
         (newfreq >=    50000) ?    50000 :
         (newfreq >=    10000) ?    10000 :
         (newfreq >=     5000) ?     5000 :
         (newfreq >=     1000) ?     1000 :
         (newfreq >=      500) ?      500 :
         (newfreq >=      100) ?      100 :
         (newfreq >=       50) ?       50 :
         (newfreq >=       10) ?       10 :
         (newfreq >=        5) ?        5 : 0;  // 0==off
 
    txret =  pg64tofpga(pctx);   // This peripheral's context
    if (txret != 0) {
        // the send of the RGB data did not succeed.  This probably
        // means the input buffer to the USB port is full.  Tell the
        // user of the problem.
        ret = snprintf(buf, *plen, E_WRFPGA);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // Start timer to look for a write response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    return;
}


/**************************************************************
 * userlength():  - The user is setting or getting the pattern
 * repeat length
 **************************************************************/
static void userlength(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    PG64DEV *pctx;     // our local info
    int      ret;      // return count
    int      newlength; // new pattern length
    int      txret;    // ==0 if the packet went out OK

    pctx = (PG64DEV *) pslot->priv;

    if (cmd == PCGET) {
        ret = snprintf(buf, *plen, "%d\n", pctx->length);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // User is setting the frequency
    ret = sscanf(val, "%d", &newlength);
    if ((ret != 1) ||
        (newlength < 1) ||
        (newlength > MXDAT)) {
        ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
        *plen = ret;
        return;
    }
    pctx->length = newlength;

    txret =  pg64tofpga(pctx);   // This peripheral's context
    if (txret != 0) {
        // the send of the RGB data did not succeed.  This probably
        // means the input buffer to the USB port is full.  Tell the
        // user of the problem.
        ret = snprintf(buf, *plen, E_WRFPGA);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // Start timer to look for a write response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    return;
}


/**************************************************************
 * pg64tofpga():  - Send hex pattern to the FPGA card.  Return
 * zero on success
 **************************************************************/
static int pg64tofpga(
    PG64DEV *pctx)    // This peripheral's context
{
    PC_PKT   pkt;      // send write and read cmds to the pg64
    SLOT    *pmyslot;  // This peripheral's slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      i;        // loop counter to copy RGB hex data
    char     c;        // hex digit to convert
    int      hex;      // hex value
    int      txret;    // ==0 if the packet went out OK

    pmyslot = pctx->pslot;
    pmycore = pmyslot->pcore;

    // Got a new value for the outputs.  Send down to the card.
    // Build and send the write command to set the patgen64.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = PG64_PATTERN;
    pkt.count = MXDAT + 2;   // pattern of 64 plus 2 config registers
    for (i = 0; i < MXDAT; i++) {
        // Convert hex char to hex integer
        c = pctx->pattern[i];
        hex = ((c <= '9') && (c >= '0')) ? (c - '0') :         // 0-9?
              ((c <= 'f') && (c >= 'a')) ? (10 + (c - 'a')) :  // a-f?
               (10 + (c - 'A'));          // A-F.
        pkt.data[i] = hex;
    }
    pkt.data[MXDAT] = // reg #64
        (pctx->freq == 20000000) ? 1 :
        (pctx->freq == 10000000) ? 2 :
        (pctx->freq ==  5000000) ? 3 :
        (pctx->freq ==  1000000) ? 4 :
        (pctx->freq ==   500000) ? 5 :
        (pctx->freq ==   100000) ? 6 :
        (pctx->freq ==    50000) ? 7 :
        (pctx->freq ==    10000) ? 8 :
        (pctx->freq ==     5000) ? 9 :
        (pctx->freq ==     1000) ? 10 :
        (pctx->freq ==      500) ? 11 :
        (pctx->freq ==      100) ? 12 :
        (pctx->freq ==       50) ? 13 :
        (pctx->freq ==       10) ? 14 :
        (pctx->freq ==        5) ? 15 :
                                   0;  // zero turns the clock off
    pkt.data[MXDAT+1] = pctx->length-1;  // reg wants max count
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    return(txret);
}


/**************************************************************
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void    *timer,   // handle of the timer that expired
    PG64DEV *pctx)    // points to instance of this peripheral
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}
// end of patgen64.c
