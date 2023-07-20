/*
 *  Name: cmods7.c
 *
 *  Description: Driver for the Digilent CmodS7 FPGA card
 *
 *  Hardware Registers:
 *    0: buttons    - Button values
 *    1: RGB        - RGB LED
 *   64: Drivlist     - table of 16 16-bit peripheral driver ID values
 * 
 *  Resources:
 *    buttons      - 1digit hex value of the 2 buttons
 *    rgb          - RGB value in bits 2/1/0
 *    drivlist     - List of requested drivers for this FPGA build
 *
 * Copyright:   Copyright (C) 2014-2023 Demand Peripherals, Inc.
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
#include <stdint.h>
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
        // CmodS7 register definitions
#define S7_REG_BUTTONS      0x00
#define S7_REG_LEDS         0x01
#define S7_REG_DRIVLIST     0x40
        // Resource names
#define FN_DRIVLIST         "drivlist"
#define FN_BUTTONS          "buttons"
#define FN_LEDS             "rgb"
        // Resource index numbers
#define RSC_DRIVLIST        0
#define RSC_BUTTONS         1
#define RSC_LEDS            2


/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an cmods7
typedef struct
{
    void    *pslot;                 // handle to peripheral's slot info
    uint32_t lastbutton;            // last reported value of the buttons
    int      rgb;                   // RGB in bits 2/1/0
    void    *ptimer;                // timer to watch for dropped ACK packets
    int      drivlist[NUM_CORE];    // list of peripheral IDs
} S7DEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void usercmd(int, int, char*, SLOT*, int, int*, char*);
static void getdriverlist(S7DEV *);
static int  board2tofpga(S7DEV *);
static void noAck(void *, S7DEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    S7DEV *pctx;    // our local device context

    // Allocate memory for this peripheral
    pctx = (S7DEV *) malloc(sizeof(S7DEV));
    if (pctx == (S7DEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in cmods7 initialization");
        return (-1);
    }

    // Init our S7DEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response
    pctx->rgb = 0;             // init leds to off

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_BUTTONS].name = FN_BUTTONS;
    pslot->rsc[RSC_BUTTONS].flags = IS_READABLE | CAN_BROADCAST;
    pslot->rsc[RSC_BUTTONS].bkey = 0;
    pslot->rsc[RSC_BUTTONS].pgscb = usercmd;
    pslot->rsc[RSC_BUTTONS].uilock = -1;
    pslot->rsc[RSC_BUTTONS].slot = pslot;
    pslot->rsc[RSC_LEDS].name = FN_LEDS;
    pslot->rsc[RSC_LEDS].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_LEDS].bkey = 0;
    pslot->rsc[RSC_LEDS].pgscb = usercmd;
    pslot->rsc[RSC_LEDS].uilock = -1;
    pslot->rsc[RSC_LEDS].slot = pslot;
    pslot->rsc[RSC_DRIVLIST].name = FN_DRIVLIST;
    pslot->rsc[RSC_DRIVLIST].flags = IS_READABLE;
    pslot->rsc[RSC_DRIVLIST].bkey = 0;
    pslot->rsc[RSC_DRIVLIST].pgscb = usercmd;
    pslot->rsc[RSC_DRIVLIST].uilock = -1;
    pslot->rsc[RSC_DRIVLIST].slot = pslot;
    pslot->name = "cmods7";
    pslot->desc = "The buttons and RGB LED on the CmodS7";
    pslot->help = README;

    (void) getdriverlist(pctx);

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
    S7DEV   *pctx;       // our local info
    RSC     *prsc;       // pointer to this packet's resource
    char     swval[9];   // ASCII value of buttons "x\n"
    int      swvallen;   // #chars in buttons, should be 2
    int      i;          // loop counter

    pctx = (S7DEV *)(pslot->priv);  // Our "private" data is a S7DEV

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
        (pkt->reg == S7_REG_DRIVLIST) && (pkt->count == (2 * NUM_CORE))) {
        // copy drivlist read results to drivlist table
        for (i = 0; i < NUM_CORE; i++) {
            pctx->drivlist[i] = (pkt->data[2*i] << 8) + pkt->data[2*i +1];
        }
        del_timer(pctx->ptimer);  //Got the response
        pctx->ptimer = 0;
    }

    // if not the peripheral list, must be the buttons.
    prsc = &(pslot->rsc[RSC_BUTTONS]);

    // If a read response from a user pcget command, send value to UI
    if (((pkt->cmd & PC_CMD_AUTO_MASK) != PC_CMD_AUTO_DATA) &&
        (pkt->reg == S7_REG_BUTTONS) && (pkt->count == 1)) {
        swvallen = sprintf(swval, "%1x\n", pkt->data[0]);
        send_ui(swval, swvallen, prsc->uilock);
        prompt(prsc->uilock);
        // Response sent so clear the lock
        prsc->uilock = -1;
        del_timer(pctx->ptimer);  //Got the response
        pctx->ptimer = 0;
    }

    // Process of elimination makes this an autosend button update.
    // Broadcast it if any UI are monitoring it.
    else if (prsc->bkey != 0) {
        // Pressing the buttons simultaneously can cause duplicate packet
        // to be sent up from the hardware.  We filter that out here.
        // This only needs to be done on autosend packets.
        if (pctx->lastbutton != pkt->data[0]) {
            swvallen = sprintf(swval, "%x\n", pkt->data[0]);
            // bkey will return cleared if UIs are no longer monitoring us
            bcst_ui(swval, swvallen, &(prsc->bkey));
        }
        pctx->lastbutton = pkt->data[0];
    }

    return;
}


/**************************************************************
 * usercmd():  - The user is reading the buttons, setting the
 * LED, or getting the drivlist.
 * Get the value from the CmodS7 if needed and write into the
 * the supplied buffer.
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
    S7DEV    *pctx;    // our local info
    PC_PKT    pkt;     // send write and read cmds to the cmods7
    int       ret;     // return count
    int       newled;  // new value for the RGB LED
    int       i;       // drivlist loop counter
    int       txret;   // ==0 if the packet went out OK
    CORE     *pmycore; // FPGA peripheral info


    pctx = (S7DEV *) pslot->priv;
    pmycore = pslot->pcore;


    // Is this a display update?
    if ((cmd == PCSET) && (rscid == RSC_LEDS )) {
        ret = sscanf(val, "%x", &newled);
        if ((ret != 1) || (newled > 7) || (newled < 0)) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        // send new value to FPGA
        pctx->rgb = newled;
        txret =  board2tofpga(pctx);   // Send segments to device
        if (txret != 0) {
            // the send of the new outval did not succeed.  This probably
            // means the input buffer to the serial port is full.  Tell the
            // user of the problem.
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
    }
    else if ((cmd == PCGET) && (rscid == RSC_LEDS)) {
        ret = snprintf(buf, *plen, "%x\n", pctx->rgb);
        *plen = ret;  // (errors are handled in calling routine)
    }
    else if ((cmd == PCGET) && (rscid == RSC_BUTTONS)) {
        // create a read packet to get the current value of the pins
        pkt.cmd = PC_CMD_OP_READ | PC_CMD_AUTOINC;
        pkt.core = (pslot->pcore)->core_id;
        pkt.reg = S7_REG_BUTTONS;
        pkt.count = 1;

        // send the packet.  Report any errors
        txret = pc_tx_pkt(pmycore, &pkt, 4);
        if (txret != 0) {
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }

        // Start timer to look for a read response.
        if (pctx->ptimer == 0)
            pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

        // lock this resource to the UI session cn
        pslot->rsc[RSC_BUTTONS].uilock = (char) cn;

        // Nothing to send back to the user
        *plen = 0;
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
            ret += snprintf(&(buf[ret]), (*plen-ret), "%04x ", pctx->drivlist[i]);
        }
        // replace last space with a newline
        buf[ret-1] = '\n';
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }

    return;
}


/**************************************************************
 * getdriverlist():  - Read the list of peripheral IDs in the cmods7
 **************************************************************/
static void getdriverlist(
    S7DEV *pctx)    // This peripheral's context
{
    PC_PKT   pkt;      // send write and read cmds to the cmods7
    SLOT    *pmyslot;  // This peripheral's slot info
    CORE    *pmycore;  // FPGA peripheral info

    pmyslot = pctx->pslot;
    pmycore = (CORE *)pmyslot->pcore;

    // Get the list of peripherals
    pkt.cmd = PC_CMD_OP_READ | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = S7_REG_DRIVLIST;
    pkt.count = 32;
    (void) pc_tx_pkt(pmycore, &pkt, 4);

    // Start timer to look for a read response.
    if (pctx->ptimer == 0)
        pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);

    return;
}


/**************************************************************
 * board2tofpga():  - Send seven segment values
 * zero on success
 **************************************************************/
int board2tofpga(
    S7DEV *pctx)     // This peripheral's context
{
    PC_PKT    pkt;      // send write and read cmds to the out4
    SLOT     *pmyslot;  // This peripheral's slot info
    CORE     *pmycore;  // FPGA peripheral info
    int       txret;    // ==0 if the packet went out OK

    pmyslot = pctx->pslot;
    pmycore = pmyslot->pcore;

    // Got a new value for the LEDs and segments.  Send down to the card.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = S7_REG_LEDS;
    pkt.count = 1;
    pkt.data[0] = pctx->rgb;                 // RGB value
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
    S7DEV *pctx)    // 
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}
// end of cmods7.c
