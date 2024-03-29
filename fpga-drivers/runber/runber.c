/*
 *  Name: runber.c
 *
 *  Description: Custom interface to the Seeed Studio Runber board.
 *              This peripheral is loaded in slot 0 to replace the enumerator.
 *              
 *  Hardware Registers:
 *              Reg 0-1: Switches and buttons.  Read-only.  Auto-send on change.
 *              Reg 2-3: RGB LEDs.  Read/write, red/green/blue
 *              Reg 4-7: Segment values for display #1-4
 *              Reg 64: Table of sixteen 16-bit peripherals ID numbers
 * 
 *  Resources:
 *              rgb - 4 bits of red, 4 bits of green, four bits of blue
 *              segments - four 8-bit values to set segments directly
 *              display - try to dislay the four character string
 *              switches - buttons in the low byte, switches in the high byte
 *              drivlist  - list of driver identification numbers in the FPGA
 *                          image
 */

/*
 * Copyright:   Copyright (C) 2022 by Demand Peripherals, Inc.
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
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <limits.h>              // for PATH_MAX
#include <sys/fcntl.h>
#include <sys/types.h>
#include "daemon.h"
#include "readme.h"
#include "drivlist.h"            // Relates driver ID so .so file name




/**************************************************************
 *  - Limits and defines
 **************************************************************/
        // runber register definitions
#define RUNBR_REG_SWITCH     0x00
#define RUNBR_REG_RED        0x02
#define RUNBR_REG_SEG1       0x02
#define RUNBR_REG_SEG2       0x03
#define RUNBR_REG_DRIVLIST   0x40
        // Resource names
#define FN_RGB                "rgb"
#define FN_SEGMENTS           "segments"
#define FN_DISPLAY            "display"
#define FN_DRIVLIST           "drivlist"
#define FN_SWITCHES           "switches"
        // Resource index numbers
#define RSC_RGB               0
#define RSC_SEGMENTS          1
#define RSC_DISPLAY           2
        // prsc fails if resource #0 in slot #0 can broadcast
#define RSC_SWITCHES          3
#define RSC_DRIVLIST          4
        // What we are is a ...
#define PLUGIN_NAME        "runber"
#define MX_MSGLEN 1000
        // The display has 4 digits
#define NDIGITS       4




/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an bb4io
typedef struct
{
    void    *pslot;      // handle to peripheral's slot info
    int      switches;   // Last reported value of the buttons/switches
    int      red;        // red bit for the RGB displays
    int      green;      // green bit for the RGB displays
    int      blue;       // blue bit for the RGB displays
    char     text[(NDIGITS*2) +1];  // Display as 4 characters
    int      segs[NDIGITS];    // Array of segment values
    void    *ptimer;     // timer to watch for dropped ACK packets
    int      drivlist[NUM_CORE];  // list of peripheral IDs
} RUN2DEV;


    // character to 7-segment mapping
typedef struct
{
    char sym;               // character to map
    int  segval;            // 7 segment equivalent
} SYMBOL;

SYMBOL symbols[] = {   // segments MSB -> pgfedcba <- LSB
    {'0', 0x3f }, {'1', 0x06 }, {'2', 0x5b }, {'3', 0x4f },
    {'4', 0x66 }, {'5', 0x6d }, {'6', 0x7d }, {'7', 0x07 },
    {'8', 0x7f }, {'9', 0x67 }, {'a', 0x77 }, {'b', 0x7c },
    {'c', 0x39 }, {'d', 0x5e }, {'e', 0x79 }, {'f', 0x71 },
    {'A', 0x77 }, {'B', 0x7c }, {'C', 0x39 }, {'D', 0x5e },
    {'E', 0x79 }, {'F', 0x71 }, {'o', 0x5c }, {'L', 0x38 },
    {'r', 0x50 }, {'h', 0x74 }, {'H', 0x76 }, {'-', 0x40 },
    {' ', 0x00 }, {'_', 0x08 }, {'u', 0x1c }, {'.', 0x00 }
};
#define NSYM (sizeof(symbols) / sizeof(SYMBOL))


/**************************************************************
 *  - Function prototypes and externs
 **************************************************************/
extern CORE Core[];
static void usercmd(int, int, char*, SLOT*, int, int*, char*);
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void text_to_segs(char *, int *);
static int  runbertofpga(RUN2DEV *);
static void noAck(void *, RUN2DEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);



/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    RUN2DEV *pctx;     // our local device context

    // Allocate memory for this peripheral
    pctx = (RUN2DEV *) malloc(sizeof(RUN2DEV));
    if (pctx == (RUN2DEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in runber initialization");
        return (-1);
    }

    // Init our RUNDEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response
    pctx->red = 0;
    pctx->green = 0;
    pctx->blue = 0;

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_RGB].name = FN_RGB;
    pslot->rsc[RSC_RGB].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_RGB].bkey = 0;
    pslot->rsc[RSC_RGB].pgscb = usercmd;
    pslot->rsc[RSC_RGB].uilock = -1;
    pslot->rsc[RSC_RGB].slot = pslot;
    pslot->rsc[RSC_SWITCHES].name = FN_SWITCHES;
    pslot->rsc[RSC_SWITCHES].flags = IS_READABLE | CAN_BROADCAST;
    pslot->rsc[RSC_SWITCHES].bkey = 0;
    pslot->rsc[RSC_SWITCHES].pgscb = usercmd;
    pslot->rsc[RSC_SWITCHES].uilock = -1;
    pslot->rsc[RSC_SWITCHES].slot = pslot;
    pslot->rsc[RSC_DISPLAY].name = FN_DISPLAY;
    pslot->rsc[RSC_DISPLAY].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_DISPLAY].bkey = 0;
    pslot->rsc[RSC_DISPLAY].pgscb = usercmd;
    pslot->rsc[RSC_DISPLAY].uilock = -1;
    pslot->rsc[RSC_DISPLAY].slot = pslot;
    pslot->rsc[RSC_SEGMENTS].name = FN_SEGMENTS;
    pslot->rsc[RSC_SEGMENTS].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_SEGMENTS].bkey = 0;
    pslot->rsc[RSC_SEGMENTS].pgscb = usercmd;
    pslot->rsc[RSC_SEGMENTS].uilock = -1;
    pslot->rsc[RSC_SEGMENTS].slot = pslot;
    pslot->rsc[RSC_DRIVLIST].name = FN_DRIVLIST;
    pslot->rsc[RSC_DRIVLIST].flags = IS_READABLE;
    pslot->rsc[RSC_DRIVLIST].bkey = 0;
    pslot->rsc[RSC_DRIVLIST].pgscb = usercmd;
    pslot->rsc[RSC_DRIVLIST].uilock = -1;
    pslot->rsc[RSC_DRIVLIST].slot = pslot;
    pslot->name = "runber";
    pslot->desc = "Runber on-board peripherals";
    pslot->help = README;

    return (0);
}


/**************************************************************
 * packet_hdlr():  - Handle incoming packets from the FPGA board
 **************************************************************/
static void packet_hdlr(
    SLOT    *pslot,      // handle for our slot's internal info
    PC_PKT  *pkt,        // the received packet
    int      len)        // number of bytes in the received packet
{
    RUN2DEV *pctx;       // our local info
    RSC     *prsc;       // pointer to this packet's resource
    char     swstate[9]; // ASCII value of switch state "xx\n"
    int      switchlen;  // #chars in swstate, should be 3
    int      i;          // loop counter

    pctx = (RUN2DEV *)(pslot->priv);  // Our "private" data is a RUN2DEV

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  //Got the ACK
            pctx->ptimer = 0;
        }
        return;
    }

    // If a read response from a user pcget command, send value to UI
    if (((pkt->cmd & PC_CMD_AUTO_MASK) != PC_CMD_AUTO_DATA) &&
        (pkt->reg == RUNBR_REG_DRIVLIST) && (pkt->count == (2 * NUM_CORE))) {
        // copy drivlist read results to drivlist table
        for (i = 0; i < NUM_CORE; i++) {
            pctx->drivlist[i] = (pkt->data[2*i] << 8) + pkt->data[2*i +1];
        }
        del_timer(pctx->ptimer);  //Got the response
        pctx->ptimer = 0;
    }

    // if not the peripheral list, must be the switches.
    prsc = &(pslot->rsc[RSC_SWITCHES]);

    // If a read response from a user pcget command, send value to UI
    if (((pkt->cmd & PC_CMD_AUTO_MASK) != PC_CMD_AUTO_DATA) &&
        (pkt->reg == RUNBR_REG_SWITCH) && (pkt->count == 2)) {
        switchlen = sprintf(swstate, "%02x %02x\n", pkt->data[0], pkt->data[1]);
        send_ui(swstate, switchlen, prsc->uilock);
        prompt(prsc->uilock);
        // Response sent so clear the lock
        prsc->uilock = -1;
        del_timer(pctx->ptimer);  //Got the response
        pctx->ptimer = 0;
    }

    // Process of elimination makes this an autosend update.
    // Broadcast it if any UI are monitoring it.
    else if ((prsc->bkey != 0) && (pkt->count == 2)) {
        switchlen = sprintf(swstate, "%02x %02x\n", pkt->data[0], pkt->data[1]);
        // bkey will return cleared if UIs are no longer monitoring us
        bcst_ui(swstate, switchlen, &(prsc->bkey));
    }

    return;
}


/**************************************************************
 * usercmd():  - The user is reading the drivlist.
 **************************************************************/
static void usercmd(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    RUN2DEV  *pctx;    // our local info
    PC_PKT    pkt;     // send write and read cmds to the Runber board
    int       txret;   // ==0 if the packet went out OK
    int       ret;     // return count
    int       i;       // loop counter
    int       rgb;     // new RBG value from user
    int       segs0;   // new value for display segments
    int       segs1;   // new value for display segments
    int       segs2;   // new value for display segments
    int       segs3;   // new value for display segments


    pctx = pslot->priv;

    // Is this a display update?
    if ((cmd == PCSET) && (rscid == RSC_DISPLAY )) {
        strncpy(pctx->text, val, (2 * NDIGITS));
        pctx->text[(2 * NDIGITS)] = (char) 0;
        text_to_segs(pctx->text, pctx->segs);

        txret =  runbertofpga(pctx);   // Send LED and segments to device
        if (txret != 0) {
            // the send of the new outval did not succeed.  This probably
            // means the input buffer to the USB port is full.  Tell the
            // user of the problem.
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
    }
    else if ((cmd == PCGET) && (rscid == RSC_DISPLAY)) {
        ret = snprintf(buf, *plen, "%s\n", pctx->text);
        *plen = ret;  // (errors are handled in calling routine)
    }
    else if ((cmd == PCGET) && (rscid == RSC_DRIVLIST)) {
        // verify there is room in the buffer for the output.  Each
        // peripheral ID is four hex characters plus a space/newline + null.
        if (*plen < ((5 * NUM_CORE) +10)) {
            // no room for output.  Send nothing
            *plen = 0;
            return;
        }
        ret = 0;
        for (i = 0; i < 16; i++) {
            ret += snprintf(&(buf[ret]), (*plen-ret), "%04x ", Core[i].driv_id);
        }
        // replace last space with a newline
        buf[ret-1] = '\n';
        *plen = ret;  // (errors are handled in calling routine)
    }
    else if ((cmd == PCGET) && (rscid == RSC_SWITCHES)) {
        // create a read packet to get the current value of the pins
        pkt.cmd = PC_CMD_OP_READ | PC_CMD_AUTOINC;
        pkt.core = (pslot->pcore)->core_id;
        pkt.reg = RUNBR_REG_SWITCH;
        pkt.count = 2;

        // send the packet.  Report any errors
        txret = pc_tx_pkt((pslot->pcore), &pkt, 4);
        if (txret != 0) {
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }

        // Start timer to look for a read response.
        if (pctx->ptimer == 0)
            pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

        // lock this resource to the UI session cn
        pslot->rsc[RSC_SWITCHES].uilock = (char) cn;

        // Nothing to send back to the user yet
        *plen = 0;
    }
    else if ((cmd == PCSET) && (rscid == RSC_RGB)) {
        ret = sscanf(val, "%x", &rgb);
        if ((ret != 1) || (rgb < 0) || (rgb > 0xfff)) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        pctx->red = (rgb >> 8);
        pctx->green = (rgb >> 4) & (0x0f);
        pctx->blue = rgb & 0x0f;

        txret =  runbertofpga(pctx);   // This peripheral's context
        if (txret != 0) {
            // the send of the new outval did not succeed.  This probably
            // means the input buffer to the USB port is full.  Tell the
            // user of the problem.
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
    }
    else if ((cmd == PCGET) && (rscid == RSC_RGB)) {
        ret = snprintf(buf, *plen, "%1x%1x%1x\n", pctx->red, pctx->green, pctx->blue);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    else if ((cmd == PCSET) && (rscid == RSC_SEGMENTS)) {
        ret = sscanf(val, "%x %x %x %x", &segs3, &segs2, &segs1, &segs0);
        if ((ret != 4) || (segs3 < 0) || (segs3 > 0xff) ||
                          (segs2 < 0) || (segs2 > 0xff) ||
                          (segs1 < 0) || (segs1 > 0xff) ||
                          (segs0 < 0) || (segs0 > 0xff)) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        pctx->segs[0] = segs0;
        pctx->segs[1] = segs1;
        pctx->segs[2] = segs2;
        pctx->segs[3] = segs3;

        txret =  runbertofpga(pctx);   // This peripheral's context
        if (txret != 0) {
            // the send of the new outval did not succeed.  This probably
            // means the input buffer to the USB port is full.  Tell the
            // user of the problem.
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
    }
    else if ((cmd == PCGET) && (rscid == RSC_SEGMENTS)) {
        ret = snprintf(buf, *plen, "%02x %02x\n", pctx->segs[0], pctx->segs[1]);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    return;
}


/**************************************************************
 * text_to_segs():  - Convert the given text to its 7-segment
 * equivalent.
 **************************************************************/
static void text_to_segs(char *text, int *segs)
{
    int   i;           // index into segs[]
    int   j;           // index into symbols[]
    int   k;           // index into text

    k = 0;
    for (i = 0; i < NDIGITS; i++) {
        segs[i] = 0;

        for (j = 0; j < NSYM; j++) {
            if (text[k] == symbols[j].sym) {
                segs[i] = symbols[j].segval;
                break;
            }
        }

        if ((text[k] != '.') && (text[k+1] == '.')) {
            segs[i] |= 0x80;     // decimal point is MSB of segments
            k++;
        }
        k++;
    }
}


/**************************************************************
 * runbertofpga():  - Send both the rgb LED values and the 
 * seven segment values to the FPGA
 * zero on success
 **************************************************************/
int runbertofpga(
    RUN2DEV *pctx)    // This peripheral's context
{
    PC_PKT   pkt;      // send write and read cmds to the out4
    SLOT    *pmyslot;  // This peripheral's slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK

    pmyslot = pctx->pslot;
    pmycore = pmyslot->pcore;

    // Got a new value for the LEDs and segments.  Send down to the card.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = RUNBR_REG_RED;
    pkt.count = 6;
    pkt.data[0] = pctx->red;
    pkt.data[1] = (pctx->green << 4) | (pctx->blue);
    pkt.data[2] = pctx->segs[0];             // right hand digit
    pkt.data[3] = pctx->segs[1];
    pkt.data[4] = pctx->segs[2];
    pkt.data[5] = pctx->segs[3];             // leftmost digit
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    // Start timer to look for a write response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    return(txret);
}


/**************************************************************
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void     *timer,   // handle of the timer that expired
    RUN2DEV  *pctx)    // 
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}

// end of runber.c
