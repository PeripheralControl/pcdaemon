/*
 *  Name: fgen.c
 *
 *  Description: Driver for the function generator peripheral
 *
 *  The fgen is a 2 MHz function generator that uses eight FPGA pins to
 *  drive an 8 bit R-2R DAC.  The high 8 bits of a phase accumulator
 *  drives the outputs directly (square, triangle) or go into a sine
 *  lookup table to get the sine of the phase.
 *
 *  Hardware Registers:
 *      0:  Mode in low two bits:
 *          0 -- off 
 *          1 -- sine
 *          2 -- triangle
 *          3 -- square
 *      1:  low byte of rising 31 bit phase offset
 *      2:  low mid byte of rising 31 bit phase offset
 *      3:  high mid byte of rising 31 bit phase offset
 *      4:  high byte of rising 31 bit phase offset
 *      5:  low byte of falling 31 bit phase offset
 *      6:  low mid byte of falling 31 bit phase offset
 *      7:  high mid byte of falling 31 bit phase offset
 *      8:  high byte of falling 31 bit phase offset
 *
 *  Resources:
 *    config    - type of output, frequency, symmetry
 *
 */

/*
 * Copyright:   Copyright (C) 2018-2026 Demand Peripherals, Inc.
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
        // FGEN register definitions
#define FGEN_REG_TYPE       0     // type of waveform
#define FGEN_REG_OFFSETR    1     // low byte of phase increment
#define FGEN_REG_OFFSETF    5     // of rising and falling half cycles
        // misc constants
#define MAX_LINE_LEN        100
        // Resources
#define RSC_CONFIG          0
        // Slot related defines
#define MAXTM  4097
#define SLOTTM 256

/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of an fgen
typedef struct
{
    void    *pslot;    // handle to peripheral's slot info
    int      otype;    // output type 0-3 = off,sine,triangle,square
    float    freq;     // output frequency
    float    symmetry; // percent weight to first half of cycle
    void    *ptimer;   // timer to watch for dropped ACK packets
} FGENDEV;

/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void fgenuser(int, int, char*, SLOT*, int, int*, char*);
static int fgentofpga(FGENDEV  *);
static void noAck(void *, FGENDEV *);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    FGENDEV  *pctx;    // our local device context

    // Allocate memory for this peripheral
    pctx = (FGENDEV *) malloc(sizeof(FGENDEV));
    if (pctx == (FGENDEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in fgen initialization");
        return (-1);
    }

    // Init our FGENDEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response

    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_CONFIG].name = "config";
    pslot->rsc[RSC_CONFIG].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CONFIG].bkey = 0;
    pslot->rsc[RSC_CONFIG].pgscb = fgenuser;
    pslot->rsc[RSC_CONFIG].uilock = -1;
    pslot->rsc[RSC_CONFIG].slot = pslot;
    pslot->name = "fgen";
    pslot->desc = "2 MHz function generator";
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
    FGENDEV *pctx;     // our local info

    pctx = (FGENDEV *)(pslot->priv);  // Our "private" data is a FGENDEV

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  //Got the ACK
            pctx->ptimer = 0;
        }
        return;
    }

    // There are no other packets from the fgen FPGA code so if we
    // get here there is a problem.  Log the error.
    pclog("invalid fgen packet from board to host");

    return;
}


/**************************************************************
 * fgenuser():  - The user is reading or writing the fgen config.
 * Get the value and update the fgen registers or write the saved
 * value to the supplied buffer.
 **************************************************************/
static void fgenuser(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    FGENDEV *pctx;     // our local info
    int      ret;      // return count
    int      newtype;  // new output type
    float    newfreq;  // new value to assign the fgen
    float    newsymm;  // new value for symmetry
    int      txret;    // ==0 if the packet went out OK
    char     ibuf[MAX_LINE_LEN];

    pctx = (FGENDEV *) pslot->priv;

    // print individual pulse width
    if ((cmd == PCGET) && (rscid == RSC_CONFIG)) {
        ret = snprintf(buf, *plen, "%f %f %s\n", 
                  pctx->freq, pctx->symmetry,
                 ((pctx->otype == 0) ? "off" : (pctx->otype == 1) ? "sine" :
                  (pctx->otype == 2) ? "triangle" : "square"));
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }
    else if ((cmd == PCSET) && (rscid == RSC_CONFIG)) {
        ret = sscanf(val, "%f %f %s", &newfreq, &newsymm, ibuf);
        // frequency must be one of the valid values
        if ((ret != 3) || (newfreq >= 20000000) || (newfreq < 0.04) ||
             (newsymm > 100) || (newsymm < 0))
        {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        newtype = -1;  
        if (strncmp("off", ibuf, 3) == 0)
            newtype = 0;
        else if (strncmp("sine", ibuf, 4) == 0)
            newtype = 1;
        else if (strncmp("triangle", ibuf, 8) == 0)
            newtype = 2;
        else if (strncmp("square", ibuf, 6) == 0)
            newtype = 3;
        if (newtype == -1) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }

        // Valid new frequency, symmetry, and waveform
        pctx->freq = newfreq;
        pctx->symmetry = newsymm;
        pctx->otype = newtype;
    }

    txret =  fgentofpga(pctx);   // This peripheral's context
    if (txret != 0) {
        // the send of the new value did not succeed.  This probably
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
 * fgentofpga():  - Send new fgen config to the FPGA card.
 *
 * The phase accumulator has 32 bits so each cycle take 2^32
 * LSB steps.  The phase clock runs at 100 MHz.  
 *    2^32 LSB/cycle / 100000000 LSB/sec = 42.9 Hz/LSB */
#define LSB_PER_HZ 41.4748365
 /* Return zero on success 
 **************************************************************/
static int fgentofpga(
    FGENDEV  *pctx)    // This peripheral's context
{
    PC_PKT   pkt;      // send write and read cmds to the fgen
    SLOT    *pmyslot;  // This peripheral's slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK
    uint32_t risingoffset;  // phase step in first half of cycle
    uint32_t fallingoffset; // phase step in second half of cycle

    pmyslot = pctx->pslot;
    pmycore = pmyslot->pcore;

    // The frequency and symmetry determine the rising and falling
    // offset values.  The time in the rising half cycle plus the
    // time in the falling half cycle must equal the period of the
    // waveform.  Symmetry values of 0 and 100 percent force the
    // rising or falling offsets to 0x7fffffff respectively and
    // force a halving of the offset since there are only 128 steps
    // in the waveform but we want it to look like there were 256.
    if (pctx->symmetry == 0.0) {
        risingoffset = 0x7fffffff;
        fallingoffset = (int)(pctx->freq * LSB_PER_HZ / 2.0);
    } else if (pctx->symmetry == 100.0) {
        risingoffset = (int)(pctx->freq * LSB_PER_HZ / 2.0);
        fallingoffset = 0x7fffffff;
    } else {
        // 50 not 100 since this is for each half cycle
        risingoffset = (int)(pctx->freq * LSB_PER_HZ * 50.0 / pctx->symmetry);
        fallingoffset = (int)(pctx->freq * LSB_PER_HZ * 50.0 / (100.0 - pctx->symmetry));
    }

    // New pwm pulse widths.  Send down to the card.
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = FGEN_REG_TYPE;
    pkt.count = 9;

    // Store type and offsets going from low byte to high byte
    pkt.data[FGEN_REG_TYPE] = pctx->otype;
    pkt.data[FGEN_REG_OFFSETR] = risingoffset & 0xff;
    pkt.data[FGEN_REG_OFFSETR +1] = (risingoffset >> 8) & 0xff;
    pkt.data[FGEN_REG_OFFSETR +2] = (risingoffset >> 16) & 0xff;
    pkt.data[FGEN_REG_OFFSETR +3] = (risingoffset >> 24) & 0xff;
    pkt.data[FGEN_REG_OFFSETF] = fallingoffset & 0xff;
    pkt.data[FGEN_REG_OFFSETF +1] = (fallingoffset >> 8) & 0xff;
    pkt.data[FGEN_REG_OFFSETF +2] = (fallingoffset >> 16) & 0xff;
    pkt.data[FGEN_REG_OFFSETF +3] = (fallingoffset >> 24) & 0xff;

    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header +  9 data

    return(txret);
}


/**************************************************************
 * noAck():  Wrote to the board but did not get a reply.  Handle
 * the timeout for this.
 **************************************************************/
static void noAck(
    void      *timer,   // handle of the timer that expired
    FGENDEV *pctx)      // points to instance of this peripheral
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}

