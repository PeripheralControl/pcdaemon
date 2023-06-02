/*
 *  Name: ps2.c
 *
 *  Description: Driver for PS/2 keyboard or mouse
 *
 *  Hardware Registers:
 *    0-44: The bits of the received bytes.  Bits include the start bit,
 *              eight data bits, the parity bit, and the stop bit.
 * 
 *  Resources:
 *    data      - hex values of the received bytes (pccat) or one byte to send.
 *
 * Copyright:   Copyright (C) 2015-2023 Demand Peripherals, Inc.
 *              All rights reserved.
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
 *              Please contact Demand Peripherals if you wish to use this code
 *              in a non-GPLv2 compliant manner. 
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include "daemon.h"
#include "readme.h"


/**************************************************************
 *  - Limits and defines
 **************************************************************/
        // PS2 register definitions
#define PS2_REG_DATA        0x00
        // max line length from user messages and input
#define MAX_LINE_LEN        100
        // Resource index numbers
#define RSC_DATAIN          0
        // PS/2 reset command
#define PS2_RESET           0xFF


/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an ps2
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    void    *ptimer;   // timer to watch for dropped ACK packets
} PS2DEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void ps2xmit(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, PS2DEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    PS2DEV *pctx;      // our local device context

    // Allocate memory for this peripheral
    pctx = (PS2DEV *) malloc(sizeof(PS2DEV));
    if (pctx == (PS2DEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in ps2 initialization");
        return (-1);
    }

    // Init our PS2DEV structure
    pctx->pslot = pslot;        // our instance of a peripheral

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add handlers for user visible resources
    pslot->rsc[RSC_DATAIN].name = "data";
    pslot->rsc[RSC_DATAIN].flags = IS_WRITABLE | CAN_BROADCAST;
    pslot->rsc[RSC_DATAIN].bkey = 0;
    pslot->rsc[RSC_DATAIN].pgscb = ps2xmit;
    pslot->rsc[RSC_DATAIN].uilock = -1;
    pslot->rsc[RSC_DATAIN].slot = pslot;
    pslot->name = "ps2";
    pslot->desc = "PS/2 keyboard input";
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
    RSC    *prsc;      // pointer to this slot's data resource
    PS2DEV *pctx;      // context for this peripheral
    char    buf[MAX_LINE_LEN];
    int     bufidx;    // index into output buffer
    int     nchar;     // number of keyboard/mouse bytes received
    int     parity;    // Sum of one's in received byte
    int     value;     // value of the scan code
    int     i,j;       // loop counters

    prsc = &(pslot->rsc[RSC_DATAIN]);
    pctx = (PS2DEV *)(pslot->priv);

    //  write response packet for command byte.  Each byte has 11 bits
    if  (((pkt->cmd & PC_CMD_AUTO_MASK) != PC_CMD_AUTO_DATA) &&
         (pkt->reg == PS2_REG_DATA) && (pkt->count == 11)) {
        del_timer(pctx->ptimer);  //Got the response
        return;
    }

    // Sanity check.   A valid packet will have a multiple of 10
    // bytes, be from the reg #0, and be an autosend
    if (((pkt->cmd & PC_CMD_OP_MASK) != PC_CMD_OP_READ) ||
        (pkt->reg != PS2_REG_DATA) || (pkt->count % 11 != 0)) {
        pclog("invalid ps2 packet from board to host");
        return;
    }

    bufidx = 0;
    nchar = pkt->count / 11;   // Usually 1 keyboard byte or 3 mouse bytes
    for (i = 0; i < nchar; i++) {
        parity = 1;
        value = 0;
        for (j = 8; j >= 1; j--) {  // end at 1 to skip start bit, msb is last bit
            value = (value << 1) + pkt->data[(i * 11) + j];
            parity = parity ^ pkt->data[(i * 11) + j];
        }
        // sanity check the received byte
        if ((pkt->data[i * 11] != 0) ||             // start bit must be 0
            (pkt->data[(i * 11) + 9] != parity) ||  // matching parity
            (pkt->data[(i * 11) + 10] != 1)) {      // stop bit must be 1
            pclog("invalid ps2 packet from board to host");
            return;
        }
        // Add received byte to output string
        bufidx += sprintf(&(buf[bufidx]), "%02x ", value);
    }
    bufidx += sprintf(&(buf[bufidx]), "\n");

    if (prsc->bkey != 0) {
        // bkey will return cleared if UIs are no longer monitoring us
        bcst_ui(buf, bufidx, &(prsc->bkey));
    }
    return;
}



/**************************************************************
 * ps2xmit():  - The user is sending an PS/2 command.  Get the
 * value, break it into individual bits, and send it down to
 * the FPGA card.
 **************************************************************/
static void ps2xmit(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    PC_PKT   pkt;      // packet to the FPGA
    PS2DEV  *pctx;     // our local info
    CORE    *pmycore;  // FPGA peripheral info
    int      ret;      // return count
    int      xmitval;  // byte to send to the keyboard
    int      txret;    // ==0 if the packet went out OK
    int      i;        // generic loop counter 
    int      parity = 1;   // odd parity

    pctx = (PS2DEV *) pslot->priv;
    pmycore = pslot->pcore;

    // Return if not a PCSET command.  Must be from a pccat.
    if (cmd != PCSET) {
        return;
    }

    // Get command byte from user
    ret = sscanf(val, "%x", &xmitval);
    if (ret != 1) {
        ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
        *plen = ret;
        return;
    }

    // Build and send the write command to the PS/2
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = PS2_REG_DATA;
    pkt.count = 11;  // start, 8 data, parity, stop

    // Break the value into bits and load the 11 bits in to registers.
    // Data is sent LSB first and with odd parity.
    pkt.data[0] = 0;                       // start bit is low
    for (i = 1; i < 9; i++) {
        pkt.data[i] = xmitval & 0x01;
        parity = parity ^ pkt.data[i];
        xmitval = xmitval >> 1;
    }
    pkt.data[9] = parity;                  // parity bit
    pkt.data[10] = 1;                      // stop bit is high

    // Packet is ready, now send it
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data
    if (txret != 0) {
        // the send of the byte did not succeed.  This probably means
        // the input buffer to the USB/serial port is full.
        // Tell the user of the problem.
        ret = snprintf(buf, *plen, E_WRFPGA);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    // Start timer to look for a write response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    *plen = 0;   // no response to user on xmit

    return;
}


/**************************************************************
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void    *timer,   // handle of the timer that expired
    PS2DEV  *pctx)    // the context of this peripheral
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}
// end of ps2.c
