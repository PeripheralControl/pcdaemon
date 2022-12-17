/*
 *  Name: rcc.c
 *
 *  Description: Resistor Capacitor analog to digital Converter (rcc)
 *
 *  Hardware Registers:
 *    0-7 :    - time for 0->1 or 1->0 transition
 *    8   :    - Config
 *             bit 6:     polarity.  Set to 1 for a 1->0 transition
 *             Bits 4-5:  clock source. 0=10MHz, 1=1MHz, 2=100KHz, 3=10KHz
 *             Bits 0-3:  Sample period in units of 10ms from 1 to 15.  0 is off
 * 
 *  Resources:
 *    rccval   - RCC times as 4/8 two digit hex numbers
 *    config   - polarity, clock rate, update period
 */

/*
 * Copyright:   Copyright (C) 2014-2022 Demand Peripherals, Inc.
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

/*
 *    The rcc provides 4 or 8 channels of input from an Resistor/Capacitor
 *    discharge circuit like that of the  Pololu QTR-RC sensor.
 *    The sensors work by charging a capacitor to Vcc (3.3 volts in our case)
 *    and monitoring the capacitor discharge.  The discharge rate depends on
 *    the amount of current flowing into from a resistor or a phototransistor.
 *    The time to discharge from 1 to 0 is reports as the reading.  This is
 *    a simple but relatively inaccurate analog to digital converter.
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
        // RCC register definitions
#define RCC_DATA            0x00
#ifdef RCC4
    #define RCC_CONFIG      0x04
    #define NPINS           4
#else
    #define RCC_CONFIG      0x08
    #define NPINS           8
#endif
        // resource names and numbers
#define FN_DATA             "rccval"
#define FN_CONFIG           "config"
#define RSC_DATA            0
#define RSC_CONFIG          1


/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an rcc
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    uint8_t  update;   // update rate for sampling 0=off, 1=10ms, ....
    uint8_t  clksrc;   // Clock rate. 0=10M, 1=1M, 2=100K, 3=10k
    uint8_t  polarity; // ==1 for a 1->0 transition
    void    *ptimer;   // timer to watch for dropped ACK packets
} RCCDEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void userconfig(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, RCCDEV *);
static void sendconfigtofpga(RCCDEV *, int *plen, char *buf);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    RCCDEV  *pctx;    // our local device context

    // Allocate memory for this peripheral
    pctx = (RCCDEV *) malloc(sizeof(RCCDEV));
    if (pctx == (RCCDEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in rcc initialization");
        return (-1);
    }

    // Init our RCCDEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->update = 0;          // default value matches power up default==off
    pctx->clksrc = 0;          // 10MHz
    pctx->polarity = 0;        // watch for a 0->1 transition
    pctx->ptimer = 0;          // set while waiting for a response

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_DATA].name = FN_DATA;
    pslot->rsc[RSC_DATA].flags = CAN_BROADCAST;
    pslot->rsc[RSC_DATA].bkey = 0;
    pslot->rsc[RSC_DATA].pgscb = 0;
    pslot->rsc[RSC_DATA].uilock = -1;
    pslot->rsc[RSC_DATA].slot = pslot;
    pslot->rsc[RSC_CONFIG].name = FN_CONFIG;
    pslot->rsc[RSC_CONFIG].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CONFIG].bkey = 0;
    pslot->rsc[RSC_CONFIG].pgscb = userconfig;
    pslot->rsc[RSC_CONFIG].uilock = -1;
    pslot->rsc[RSC_CONFIG].slot = pslot;
    #ifdef RCC4
        pslot->name = "rcc4";
    #else
        pslot->name = "rcc8";
    #endif
    pslot->desc = "Resistor Capacitor discharge timer";
    pslot->help = README;


    // Send the update rate to the peripheral to turn it off.
    // Ignore return value since there's no user connection and
    // system errors are sent to the logger.
    sendconfigtofpga(pctx, (int *) 0, (char *) 0);

    return (0);
}

/**************************************************************
 * packet_hdlr():  - Handle incoming packets from the FPGA board
 **************************************************************/
static void packet_hdlr(
    SLOT    *pslot,    // handle for our slot's internal info
    PC_PKT  *pkt,      // the received packet
    int      len)      // number of bytes in the received packet
{
    RCCDEV   *pctx;    // our local info
    RSC      *prsc;    // pointer to this slot's counts resource
    char      qstr[100]; // up to eight space separated two digit numbers
    int       qlen;    // length of line to send


    pctx = (RCCDEV *)(pslot->priv);  // Our "private" data
    prsc = &(pslot->rsc[RSC_DATA]);

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        del_timer(pctx->ptimer);  // Got the ACK
        pctx->ptimer = 0;
        return;
    }

    // Do a sanity check on the received packet.  Only reads from
    // the data register should come in since we don't ever read
    // the config
    // Packet has one byte per input pin.
    if ((pkt->reg != RCC_DATA) || (pkt->count != NPINS)) {
        pclog("invalid rcc packet from board to host");
        return;
    }

    // Process of elimination makes this an autosend packet.
    // Broadcast it if any UI are monitoring it.
    if (prsc->bkey != 0) {
        #ifdef RCC4
            qlen = sprintf(qstr, "%02x %02x %02x %02x\n", pkt->data[0], pkt->data[1],
                   pkt->data[2], pkt->data[3]);
        #else
            qlen = sprintf(qstr, "%02x %02x %02x %02x %02x %02x %02x %02x\n", pkt->data[0],
                   pkt->data[1], pkt->data[2], pkt->data[3], pkt->data[4], pkt->data[5],
                   pkt->data[6], pkt->data[7]);
        #endif
        // bkey will return cleared if UIs are no longer monitoring us
        bcst_ui(qstr, qlen, &(prsc->bkey));
        return;
    }

    return;
}


/**************************************************************
 * userconfig():  - The user is reading or setting the configuration
 **************************************************************/
static void userconfig(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    RCCDEV  *pctx;     // our local info
    int      ret;      // return count
    int      nupdate;  // new value for the update rate
    int      nclk;     // new value for the clock source
    int      npol;     // new value for the polarity

    pctx = (RCCDEV *) pslot->priv;

    if ((cmd == PCGET) && (rscid == RSC_CONFIG)) {
        // Give config to user
        ret = snprintf(buf, *plen, "%d %s %d\n", pctx->polarity,
               (pctx->clksrc ==0) ? "10000000" :
               (pctx->clksrc ==1) ? "1000000" :
               (pctx->clksrc ==2) ? "100000" : "10000",
               (pctx->update * 10));
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }
    else if ((cmd == PCSET) && (rscid == RSC_CONFIG)) {
        // Update period is 0 to 150 
        ret = sscanf(val, "%d %d %d", &npol, &nclk, &nupdate);
        if ((ret != 3) || (nupdate < 0) || (nupdate > 150) ||
            ((npol != 0) && (npol != 1)) || ((nclk != 10000000) &&
             (nclk != 1000000) && (nclk != 100000) && (nclk != 10000))) {
            ret = snprintf(buf, *plen, E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }

        pctx->polarity = npol;              // record new polarity
        pctx->clksrc = (nclk == 10000000) ? 0 :
                       (nclk == 1000000) ? 1 :
                       (nclk == 100000) ? 2 : 3;
        pctx->update = nupdate / 10;        // steps of 10 ms each
        sendconfigtofpga(pctx, plen, buf);  // send down new config
    }

    return;
}


/**************************************************************
 * sendconfigtofpga():  - Send sample period to the FPGA card. 
 * Put error messages into buf and update plen.
 **************************************************************/
static void sendconfigtofpga(
    RCCDEV   *pctx,    // This peripheral's context
    int      *plen,    // size of buf on input, #char in buf on output
    char     *buf)     // where to store user visible error messages
{
    PC_PKT   pkt;      // send write and read cmds to the rcc
    SLOT    *pslot;    // This peripheral's slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK
    int      ret;      // generic return value

    pslot = pctx->pslot;
    pmycore = pslot->pcore;

    // Write the values for the pins, direction, and interrupt mask
    // down to the card.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = RCC_CONFIG;      // send config
    pkt.count = 1;
    pkt.data[0] = (pctx->polarity << 6) + (pctx->clksrc << 4) + pctx->update;
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    if (txret != 0) {
        // the send of the new pin values did not succeed.  This
        // probably means the input buffer to the USB port is full.
        // Tell the user of the problem.
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
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void     *timer,   // handle of the timer that expired
    RCCDEV   *pctx)    // 
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}

// end of rcc.c
