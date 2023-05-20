/*
 *  Name: sndgen.c
 *
 *  Description: Device driver for the sndgen peripheral
 *
 *  Hardware Registers:
 *    Addr=0    Oscillator mode in high 4 bits. High 4 bits of osc phase step
 *    Addr=1    Oscillator phase step low byte.  One LSB=1.527 Hz
 *    Addr=2    LFO oneshot, invert and mode. High 4 bits of LFO phase step 
 *    Addr=3    LFO phase step low byte.
 *    Addr=4    LFO period  (units of 0.01 sec)
 *    Addr=5    LFO steps per update, Step size is 0.01 seconds
 *    Addr=6    Bit7=osc enable, bit6=lfsr enable, bits  5-4 is lfsr clock, Bit 3-2
 *              and 1-0 are attenuation where 00=none, 1=1/2, 2=1/4, and 3=1/8
 *
 *  Resources:
 *    config    - read/write sound generator register configuration
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
        // SND register definitions
#define SND_REG_CONFIG      0x00
        // line length from user to set SND values
#define CONFIG_LEN          100
#define FN_CONFIG           "config"
        // Resource index numbers
#define RSC_CONFIG          0
        // Hertz per LSB of the oscillator phase accumulator
#define OSC_STEP            1.527
        // Modes that match Verilog values
#define OSC_SQUARE          0
#define OSC_RAMP            1
#define OSC_TRIANGLE        2
#define OSC_OFF             3
#define OSC_INVERT          4

/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an sndgen
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    char     omode;    // oscillator mode otsud  : off, triangle, square, ramp up, ramp down, 
    int      ofreq;    // main oscillator frequency in Hertz
    int      lmode;    // LFP mode, otsud : off, triangle, square, ramp up, ramp down
    int      lfreq;    // LFO frequency span applied to oscillator in Hertz
    int      lperiod;  // LFO repeat time in 0.01 seconds up to 250
    char     l1shot;   // LFO one shot (o) or continuous (c)
    char     nfreq;    // Noise frequency ohml: off, high, medium, low
    char     oattn;    // oscillator attenuation: 0248 : none, 1/2
    char     nattn;    // noise attenuation: 0248 : 1/4, or 1/8
    void    *ptimer;   // timer to watch for dropped ACK packets
} SNDDEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void user_cb(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, SNDDEV *);
static int  configtofpga(SNDDEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    SNDDEV *pctx;    // our local device context

    // Allocate memory for this peripheral
    pctx = (SNDDEV *) malloc(sizeof(SNDDEV));
    if (pctx == (SNDDEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in sndgen initialization");
        return (-1);
    }

    // Init our SNDDEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response
    pctx->omode = 'o';      // oscillator mode otsud 
    pctx->ofreq = 1000;     // main oscillator frequency in Hertz
    pctx->lmode = 'o';      // LFP mode, otpud :
    pctx->lfreq = 100;      // LFO frequency span applied to oscillator in Hertz
    pctx->lperiod = 0;      // LFO repeat time in 0.01 seconds up to 250
    pctx->l1shot = 'o';     // LFO one shot (o) or continuous (c)
    pctx->nfreq = 'm';      // Noise frequency hml: high, medium, low
    pctx->oattn = '2';      // oscillator attenuation: 0248 : none, 1/2
    pctx->nattn = '2';      // noise attenuation: 0248 : 1/4, or 1/8

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_CONFIG].name = "config";
    pslot->rsc[RSC_CONFIG].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CONFIG].bkey = 0;
    pslot->rsc[RSC_CONFIG].pgscb = user_cb;
    pslot->rsc[RSC_CONFIG].uilock = -1;
    pslot->rsc[RSC_CONFIG].slot = pslot;
    pslot->name = "sndgen";
    pslot->desc = "Sound generator";
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
    SNDDEV *pctx;    // our local info

    pctx = (SNDDEV *)(pslot->priv);  // Our "private" data is a SNDDEV

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  //Got the ACK
            pctx->ptimer = 0;
        }
        return;
    }

    // Do a sanity check on the received packet.
    if ((pkt->reg != SND_REG_CONFIG) || (pkt->count != 1)) {
        pclog("invalid sndgen packet from board to host");
        return;
    }

    return;
}


/**************************************************************
 * user_cb():  - The user is reading or writing to the output.
 * Get the value and update the sndgen on the FPGA or read the
 * value and write it into the supplied buffer.
 **************************************************************/
static void user_cb(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    SNDDEV  *pctx;     // our local info
    int      ret;      // return count
    int      txret;    // ==0 if the packet went out OK
    char     omode;    // oscillator mode otsrf "Off, triangle, square, rising ramp, falling ramp, 
    int      ofreq;    // main oscillator frequency in Hertz
    char     lmode;    // LFP mode, otrfud : off, triangle, rising ramp, falling ramp, step up, down
    int      lfreq;    // LFO frequency span applied to oscillator in Hertz
    int      lperiod;  // LFO repeat time in 0.01 seconds up to 250
    char     l1shot;   // LFO one shot (o) or continuous (c)
    char     nfreq;    // Noise frequency hml: high, medium, low
    char     oattn;    // oscillator attenuation: 0248 : none, 1/2
    char     nattn;    // noise attenuation: 0248 : 1/4, or 1/8

    pctx = (SNDDEV *) pslot->priv;

    if (cmd == PCSET) {
        ret = sscanf(val, "%c %d %c %d %d %c %c %c %c", &omode, &ofreq,
              &lmode, &lfreq, &lperiod, &l1shot, &nfreq, &oattn, &nattn);

        // Sanity check the values.
        if ((ret != 9) ||
            ( (omode != 'o') && (omode != 't') && (omode != 's') &&
              (omode != 'r') && (omode != 'f')) ||
            ( (ofreq < 24) || (ofreq > 7000)) ||
            ( (lmode != 'o') && (lmode != 't') && (lmode != 'r') &&
              (lmode != 'f') && (lmode != 'u') && (lmode != 'd')) ||
            ( (lfreq < 0) || (lfreq > 5000)) ||
            ( (lperiod < 0) || (lperiod > 250)) ||
            ( (l1shot != 'o') && (l1shot != 'c')) ||
            ( (nfreq != 'h') && (nfreq != 'm') && (nfreq != 'l') && (nfreq != 'o')) ||
            ( (oattn != '0') && (oattn != '2') && (oattn != '4') && (oattn != '8')) ||
            ( (nattn != '0') && (nattn != '2') && (nattn != '4') && (nattn != '8'))) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        // Valid values.  Save and update the FPGA
        pctx->omode = omode;
        pctx->ofreq = ofreq;
        pctx->lmode = lmode;
        pctx->lfreq = lfreq;
        pctx->lperiod = lperiod;
        pctx->l1shot = l1shot;
        pctx->nfreq = nfreq;
        pctx->oattn = oattn;
        pctx->nattn = nattn;

        txret =  configtofpga(pctx);   // This peripheral's context
        if (txret != 0) {
            // the send of the new config did not succeed.  This probably
            // means the input buffer to the serial port is full.  Tell the
            // user of the problem.
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }

        // Start timer to look for a write response.
        if (pctx->ptimer == 0)
            pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);
    }
    else if (cmd == PCGET) {
        ret = snprintf(buf, *plen, "%c %d %c %d %d %c %c %c %c", pctx->omode,
              pctx->ofreq, pctx->lmode, pctx->lfreq, pctx->lperiod,
              pctx->l1shot, pctx->nfreq, pctx->oattn, pctx->nattn);
        *plen = ret;  // (errors are handled in calling routine)
    }

    return;
}


/**************************************************************
 * configtofpga():  - Send sound generator config to the FPGA card.
 * Return zero on success
 **************************************************************/
int configtofpga(
    SNDDEV  *pctx)       // This peripheral's context
{
    PC_PKT   pkt;        // send write and read cmds to the FPGA
    SLOT    *pmyslot;    // This peripheral's slot info
    CORE    *pmycore;    // FPGA peripheral info
    int      txret;      // ==0 if the packet went out OK
    int      ophasestep; // oscillator frequency in phase steps
    float    lphasestep; // LFO frequency in phase steps. Can be less than one.

    pmyslot = pctx->pslot;
    pmycore = pmyslot->pcore;
    ophasestep = (int)((float)pctx->ofreq / OSC_STEP);
    // LFO runs at 100 Hz so the phase step for the LFO
    // is the freq change in OSC_STEPs per the LFO period
    lphasestep = ((float)pctx->lfreq / (float)pctx->lperiod) / OSC_STEP;

    // Build and send the write command to set the sndgen.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = SND_REG_CONFIG;
    pkt.count = 7;
    pkt.data[0] = (pctx->omode == 'o') ? (OSC_OFF << 4) :
                  (pctx->omode == 's') ? (OSC_SQUARE << 4) :
                  (pctx->omode == 't') ? (OSC_TRIANGLE << 4) :
                  (pctx->omode == 'u') ? (OSC_RAMP << 4) :
                  (pctx->omode == 'd') ? ((OSC_RAMP + OSC_INVERT) << 4) :
                  0;
    pkt.data[0] |= (ophasestep >> 8 ) & 0x0f;
    pkt.data[1] = ophasestep & 0xff;
    pkt.data[2] = (pctx->l1shot == 'o') ? 0x80 : 0x00;
    pkt.data[2] |= (pctx->lmode == 'o') ? (OSC_OFF << 4) :
                  (pctx->lmode == 't') ? (OSC_TRIANGLE << 4) :
		  (pctx->lmode == 'r') ? (OSC_RAMP << 4) :
                  (pctx->lmode == 'f') ? ((OSC_RAMP + OSC_INVERT) << 4) :
		  (pctx->lmode == 'u') ? (OSC_SQUARE << 4) :
                  (pctx->lmode == 'd') ? ((OSC_SQUARE + OSC_INVERT) << 4) :
                  0;
    pkt.data[4] = pctx->lperiod;
    if ((pctx->lmode == 'u') || (pctx->lmode == 'd')) {   // square wave up or down
        pkt.data[2] |= ((int)((float)pctx->lfreq / OSC_STEP) >> 8 ) & 0x0f;
        pkt.data[3] = (int)((float)pctx->lfreq / OSC_STEP) & 0xff;
        pkt.data[5] = pctx->lperiod / 2;   // two tones
    }
    else if (lphasestep > 1) {
        pkt.data[2] |= ((int)lphasestep >> 8 ) & 0x0f;
        pkt.data[3] = (int)lphasestep & 0xff;
        pkt.data[5] = 1;     // update LFO every 0.01 seconds
    }
    else {
        pkt.data[3] = 1;
        pkt.data[5] = (int)(1 / lphasestep);  // one step less then 0.01 sec often
    }

    pkt.data[6]  = (pctx->omode != 'o') ? 0x80 : 00;   // osc enable
    pkt.data[6] |= (pctx->nfreq != 'o') ? 0x40 : 00;   // noise enable
    pkt.data[6] |= (pctx->nfreq == 'h') ? 0x20 :       // noise clk, high=10us ==2
                   (pctx->nfreq == 'm') ? 0x10 : 00;   // med=100us==1, low=1m==0
    pkt.data[6] |= (pctx->oattn == '8') ? 0x0c :       // shift by 3
                   (pctx->oattn == '4') ? 0x08 :       // shift down by 2
                   (pctx->oattn == '2') ? 0x04 : 00;   // shift down by 1 or not at all
    pkt.data[6] |= (pctx->nattn == '8') ? 0x03 :       // shift by 3
                   (pctx->nattn == '4') ? 0x02 :       // shift down by 2
                   (pctx->nattn == '2') ? 0x01 : 00;   // shift down by 1 or not at all

    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data
    return(txret);
}


/**************************************************************
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void    *timer,   // handle of the timer that expired
    SNDDEV  *pctx)    // points to instance of this peripheral
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}
// end of out4.c
