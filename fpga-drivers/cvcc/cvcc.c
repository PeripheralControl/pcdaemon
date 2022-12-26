/*
 *  Name: cvcc.c
 *
 *  Description: Driver CVCC programmable power supply by Jerry O'Keefe
 *
 *  Hardware Registers:
 *   0,1:   vlin    - Load voltage (high,low)
 *   2,3:   ilin    - Load current
 *   4,5:   vref    - PWM width of Vref
 *   6,7    per     - Period of Vref in units of 10 ns
 *   8,9:   vset    - Maximum voltage to the load
 *   10,11: iset    - Maximum current to the load
 *   12:            - Enable
 * 
 *  Resources:
 *    viout        - Maximum voltage and current settings
 *    viin         - Measured load voltage and current
 *
 * Copyright:   Copyright (C) 2022 Demand Peripherals, Inc.
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
        // CVCC register definitions
#define CVCC_REG_VIIN      0x00
#define CVCC_REG_VIOUT     0x08
        // resource names and numbers
#define FN_VIIN            "viin"
#define FN_VIOUT           "viout"
#define FN_CONF            "conf"
#define RSC_VIOUT          0
#define RSC_VIIN           1
#define RSC_CONF           2
        // Length of output string for load V/I
#define NLOADSTR           50
        // Full scale bits
#define FULLSCALE          1023

/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an cvcc
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    int      vin;      // the measured load voltage
    int      iin;      // the measured load current
    int      vout;     // the user set output voltage as a percent
    int      iout;     // the user set output current as a percent
    void    *ptimer;   // timer to watch for dropped ACK packets
} CVCCDEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void usercmd(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, CVCCDEV *);
static void sendconfigtofpga(CVCCDEV *, int *plen, char *buf);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    CVCCDEV *pctx;    // our local device context

    // Allocate memory for this peripheral
    pctx = (CVCCDEV *) malloc(sizeof(CVCCDEV));
    if (pctx == (CVCCDEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in cvcc initialization");
        return (-1);
    }

    // Init our CVCCDEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->vout = 0;
    pctx->iout = 0;
    pctx->ptimer = 0;          // set while waiting for a response


    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_VIIN].name = FN_VIIN;
    pslot->rsc[RSC_VIIN].flags = IS_READABLE | CAN_BROADCAST;
    pslot->rsc[RSC_VIIN].bkey = 0;
    pslot->rsc[RSC_VIIN].pgscb = 0;
    pslot->rsc[RSC_VIIN].uilock = -1;
    pslot->rsc[RSC_VIIN].slot = pslot;
    pslot->rsc[RSC_VIOUT].name = FN_VIOUT;
    pslot->rsc[RSC_VIOUT].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_VIOUT].bkey = 0;
    pslot->rsc[RSC_VIOUT].pgscb = usercmd;
    pslot->rsc[RSC_VIOUT].uilock = -1;
    pslot->rsc[RSC_VIOUT].slot = pslot;
    pslot->rsc[RSC_CONF].name = FN_CONF;
    pslot->rsc[RSC_CONF].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CONF].bkey = 0;
    pslot->rsc[RSC_CONF].pgscb = usercmd;
    pslot->rsc[RSC_CONF].uilock = -1;
    pslot->rsc[RSC_CONF].slot = pslot;
    pslot->name = "cvcc";
    pslot->desc = "Constant Voltage Constant Current regulator";
    pslot->help = README;

    // Send the initial V/I output and configuration to the FPGA
    // Ignore return value since there's no user connection and
    // system errors are sent to the logger.
    sendconfigtofpga(pctx, (int *) 0, (char *) 0);  // set defaults

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
    CVCCDEV *pctx;     // our local info
    RSC    *prsc;      // pointer to the V/I input resource
    char    loadstr[NLOADSTR];  // Load V/I as a string
    int     ldlen;     // length of loadstr
    float   period;    // period of PWM inputs in units of 160 ns

    pctx = (CVCCDEV *)(pslot->priv);  // Our "private" data is a CVCCDEV
    prsc = &(pslot->rsc[RSC_VIIN]);

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  //Got the ACK
            pctx->ptimer = 0;
        }
        return;
    }

    // Do a sanity check on the received packet.  Only reads from
    // the viin should come in since we don't ever read the set values
    // or the configuration
    if ((pkt->reg != CVCC_REG_VIIN) || (pkt->count != 8)) {
        pclog("invalid cvcc packet from board to host");
        return;
    }

    // Process of elimination makes this an autosend packet.
    // Broadcast it if any UI are monitoring it.
    if (prsc->bkey != 0) {
        period = (float)((pkt->data[6] << 8) + pkt->data[7]);

        ldlen = snprintf(loadstr, NLOADSTR, "%3.1f %3.1f %3.1f %3.1f\n", 
                       (100.0)*((pkt->data[0] << 8) + pkt->data[1]) / period,
                       (100.0)*((pkt->data[2] << 8) + pkt->data[3]) / period,
                       (100.0)*((pkt->data[4] << 8) + pkt->data[5]) / period,
                       (100000.0 / (period / 16.0)));
        // bkey will return cleared if UIs are no longer monitoring us
        bcst_ui(loadstr, ldlen, &(prsc->bkey));
        return;
    }

    return;
}


/**************************************************************
 * usercmd():  - The user is reading or writing the cvcc 
 * configuration or setting the output voltage and current.
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
    CVCCDEV *pctx;    // our local info
    float    newv;     // new value of the set voltage
    float    newi;     // new value of the set current
    int      ret;      // sprintf count

    pctx = (CVCCDEV *) pslot->priv;

    // A read of V or I in?
    if ((cmd == PCGET) && (rscid == RSC_VIIN )) {
        ret = snprintf(buf, *plen, "%3.1f %3.1f\n", 
                         (float)(pctx->vin)/(float) FULLSCALE,
                         (float)(pctx->iin)/(float) FULLSCALE);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }
    else if ((cmd == PCGET) && (rscid == RSC_VIOUT )) {
        ret = snprintf(buf, *plen, "%3.1f %3.1f\n", 
                         (float)(pctx->vout)/(float) FULLSCALE,
                         (float)(pctx->iout)/(float) FULLSCALE);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }
    else if ((cmd == PCSET) && (rscid == RSC_VIOUT )) {
        ret = sscanf(val, "%f %f", &newv, &newi);
        if ((ret != 2) || (newv < 0.0) || (newv > 100.0) ||
            (newi < 0.0) || (newi > 100.0)) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        pctx->vout = (newv * FULLSCALE) / 100;
        pctx->iout = (newi * FULLSCALE) / 100;
printf("new vout/iout = %d %d\n", pctx->vout,pctx->iout);
        sendconfigtofpga(pctx, plen, buf);  // send new config to board
        return;
    }
}


/**************************************************************
 * sendconfigtofpga():  - Send config values to the FPGA card.  Put
 * error messages into buf and update plen.
 **************************************************************/
static void sendconfigtofpga(
    CVCCDEV *pctx,     // This peripheral's context
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)      // where to store user visible error messages
{
    PC_PKT   pkt;      // send write and read cmds to the cvcc
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
    pkt.reg = CVCC_REG_VIOUT;   // the first reg of the three
    pkt.count = 5;
    pkt.data[0] = pctx->vout >> 8;
    pkt.data[1] = pctx->vout & 0xff;
    pkt.data[2] = pctx->iout >> 8;
    pkt.data[3] = pctx->iout & 0xff;
    pkt.data[4] = (pctx->vout != 0) && (pctx->iout != 0);
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    if (txret != 0) {
        // the send of the new config values did not succeed.  This
        // probably means the input buffer to the serial port is full.
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
    CVCCDEV *pctx)    // the peripheral with a timeout
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}

// end of cvcc.c
