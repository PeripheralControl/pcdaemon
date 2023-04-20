/*
 *  Name: vgaterm.c
 *
 *  Description: Driver for the VGA terminal using 8 FPGA pins
 *
 *  Resources:
 *    char         - output FIFO on write, char under cursor on read
 *    cursor       - style of cursor
 *    attr         - foreground/background colors and underline
 *    rowoff       - row to display after Vsync -- used for scrolling
 *
 *  Addr  Register
 *     0  Character FIFO on write, char under cursor on read
 *     1  Set cursor col location on write, get location on read
 *     2  Set cursor row location on write, get location on read
 *     3  Display row offset.  Display this row after vsync
 *     4  Cursor style. Bit0=block/underline, Bit1=invisible/visible
 *     5  Foreground color applied to all subsequent characters rgb 222
 *     6  Background color applied to all subsequent characters rgb 222
 *     7  Attributes. Bit0=underline, Bit1=blink
 *
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
#include <ctype.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include "daemon.h"
#include "readme.h"


/**************************************************************
 *  - Limits and defines
 **************************************************************/
        // VGA register definitions
#define VGA_REG_CHAR            0 // Character FIFO
#define VGA_REG_CURCOL          1 // Set cursor column 
#define VGA_REG_CURROW          2 // Set cursor row 
#define VGA_REG_ROWOFF          3 // Set row offset
#define VGA_REG_CURTYPE         4 // Cursor block if bit0=0 visible if bit1=0
#define VGA_REG_FGRGB           5 // Foreground color rgb 2:2:2
#define VGA_REG_BGRGB           6 // Background color rgb 2:2:2
#define VGA_REG_ATTR            7 // Underline if Bit0=1, blink if bit1=1
        // resource names and numbers
#define FN_CHAR                 "char"
#define FN_CURSOR               "cursor"
#define FN_ATTR                 "attr"
#define FN_ROWOFF               "rowoff"
#define RSC_CHAR                0
#define RSC_CURSOR              1
#define RSC_ATTR                2
#define RSC_ROWOFF              3

        // misc constants
#define MXLNLEN               120 // max line length to/from the user
#define NUMROW                 40
#define NUMCOL                 80

/**************************************************************
 *  - Data structures
 **************************************************************/
    // Context of this peripheral
typedef struct
{
    SLOT    *pslot;        // handle to peripheral's slot info
    void    *ptimer;       // timer to watch for dropped ACK packets
    int      currow;       // set cursor row
    int      curcol;       // set cursor column
    int      rowoff;       // row offset after vsync
    char     curvisible;   // =='v' if cursor is visible, or 'i'
    char     curstyle;     // =='b' for block cursor, or 'u'
    char     underline;    // =='u' for underline if set, or 'n'
    char     blink;        // =='b' for blink, or 'n'
    int      fgclr;        // foreground color
    int      bgclr;        // background color
    int      charlen;      // lenght of string to send to display
    char     charstr[NUMCOL]; // string to send to display
} VGADEV;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void packet_hdlr(SLOT *, PC_PKT *, int);
static void userhdlr(int, int, char*, SLOT*, int, int*, char*);
static void noAck(void *, VGADEV *);
static void sendstringtofpga(VGADEV *, int *plen, char *buf);
static void sendcursortofpga(VGADEV *, int *plen, char *buf);
static void sendattrtofpga(VGADEV *, int *plen, char *buf);
extern int  pc_tx_pkt(CORE *pcore, PC_PKT *inpkt, int len);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this peripheral
{
    VGADEV *pctx;      // our local device context

    // Allocate memory for this peripheral
    pctx = (VGADEV *) malloc(sizeof(VGADEV));
    if (pctx == (VGADEV *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in vgaterm initialization");
        return (-1);
    }

    // Init our VGADEV structure
    pctx->pslot = pslot;       // our instance of a peripheral
    pctx->ptimer = 0;          // set while waiting for a response
    pctx->currow = 1;          // cursor row = top row
    pctx->curcol = 1;          // cursor column = far left
    pctx->rowoff = 0;          // row offset after vsync
    pctx->curvisible = 0;      // cursor is visible
    pctx->curstyle = 0;        // block cursor
    pctx->fgclr = 0x3f;        // foreground color = white
    pctx->bgclr = 0;           // background color = black
    pctx->underline = 'n';     // no underline
    pctx->blink = 'n';         // no blink


    // Register this slot's packet handler and private data
    (pslot->pcore)->pcb  = packet_hdlr;
    pslot->priv = pctx;

    // Add the handlers for the user visible resources
    pslot->rsc[RSC_CHAR].name = FN_CHAR;
    pslot->rsc[RSC_CHAR].pgscb = userhdlr;
    pslot->rsc[RSC_CHAR].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CHAR].bkey = 0;
    pslot->rsc[RSC_CHAR].uilock = -1;
    pslot->rsc[RSC_CHAR].slot = pslot;
    pslot->rsc[RSC_CURSOR].name = FN_CURSOR;
    pslot->rsc[RSC_CURSOR].pgscb = userhdlr;
    pslot->rsc[RSC_CURSOR].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_CURSOR].bkey = 0;
    pslot->rsc[RSC_CURSOR].uilock = -1;
    pslot->rsc[RSC_CURSOR].slot = pslot;
    pslot->rsc[RSC_ATTR].name = FN_ATTR;
    pslot->rsc[RSC_ATTR].pgscb = userhdlr;
    pslot->rsc[RSC_ATTR].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_ATTR].bkey = 0;
    pslot->rsc[RSC_ATTR].uilock = -1;
    pslot->rsc[RSC_ATTR].slot = pslot;
    pslot->rsc[RSC_ROWOFF].name = FN_ROWOFF;
    pslot->rsc[RSC_ROWOFF].pgscb = userhdlr;
    pslot->rsc[RSC_ROWOFF].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_ROWOFF].bkey = 0;
    pslot->rsc[RSC_ROWOFF].uilock = -1;
    pslot->rsc[RSC_ROWOFF].slot = pslot;
    pslot->name = "vgaterm";
    pslot->desc = "VGA Terminal with 6 bit color";
    pslot->help = README;

    // Send the value, direction and interrupt setting to the card.
    // Ignore return value since there's no user connection and
    // system errors are sent to the logger.
    sendcursortofpga(pctx, (int *) 0, (char *) 0);  // send cursor location and style
    sendattrtofpga(pctx, (int *) 0, (char *) 0);    // send character colors and style

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
    VGADEV *pctx;      // our local info
    RSC    *prsc;      // pointer to this slot's pins resource
    int     repllen;   // reply length
    char    reply[MXLNLEN];  // reply to user

    pctx = (VGADEV *)(pslot->priv);  // Our "private" data is a VGADEV

    // Clear the timer on write response packets
    if ((pkt->cmd & PC_CMD_OP_MASK) == PC_CMD_OP_WRITE) {
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);  // Got the ACK
            pctx->ptimer = 0;
        }
        return;
    }

    // If a read response from a user pcget command, send value to UI.
    // The user has requested a read of the character under the cursor
    // including the character, the foreground and background colors,
    // and whether or not it is underlined.
    if ((pkt->cmd & PC_CMD_AUTO_MASK) != PC_CMD_AUTO_DATA) {
        if (pkt->reg == VGA_REG_CURCOL) {
            prsc = &(pslot->rsc[RSC_CURSOR]);    // FPGA is zero-indexed, so 1+
            repllen = sprintf(reply, "%3d %3d %c %c\n", 1+pkt->data[0], 1+pkt->data[1],
                      (((pkt->data[3] & 0x1) == 0) ? 'u' : 'b'),
                      (((pkt->data[3] & 0x2) == 0) ? 'i' : 'v'));
            send_ui(reply, repllen, prsc->uilock);
            prompt(prsc->uilock);
            // Response sent so clear the lock
            prsc->uilock = -1;
            del_timer(pctx->ptimer);  //Got the response
            pctx->ptimer = 0;
            return;
        }
        else if (pkt->reg == VGA_REG_CHAR) {
            prsc = &(pslot->rsc[RSC_CHAR]);
            repllen = sprintf(reply, "0x%02x 0x%02x 0x%02x %c %c\n",
                      pkt->data[0], pkt->data[5], pkt->data[6],    // char, fg, bg
                      (((pkt->data[7] & 0x1) == 0) ? 'n' : 'u'),   // underline
                      (((pkt->data[7] & 0x2) == 0) ? 'n' : 'b'));  // blink
            send_ui(reply, repllen, prsc->uilock);
            prompt(prsc->uilock);
            // Response sent so clear the lock
            prsc->uilock = -1;
            del_timer(pctx->ptimer);  //Got the response
            pctx->ptimer = 0;
            return;
        }
    }

    // There are no other packets from the vgaterm FPGA code so if we
    // get here there is a problem.  Log the error.
    pclog("invalid vgaterm packet from board to host");

    return;
}



/**************************************************************
 * userhdlr():  - Handle commands from the user
 **************************************************************/
static void userhdlr(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    VGADEV  *pctx;     // our local info
    CORE    *pmycore;  // FPGA peripheral info
    int      ret;      // return count
    int      txret;    // ==0 if the packet went out OK
    PC_PKT   pkt;      // packet to the FPGA card
    int      fg;       // foreground color
    int      bg;       // background color
    char     under;    // 'u'nderline or 'n'ot
    char     blnk;     // 'b'link or 'n'ot
    int      crow;     // cursor row
    int      ccol;     // cursor column
    int      newoff;   // new row offset
    char     style;    // 'b'lock or 'u'nderline
    char     visible;  // 'v'isible cursor or 'i'nvisible
    int      charlen;  // length of string to sent to display


    pctx = (VGADEV *) pslot->priv;
    pmycore = pslot->pcore;

    // read or write one of three resources.  Use if/else for 6 cases

    if ((cmd == PCGET) && (rscid == RSC_CHAR)) {
        // create a read packet to get the current value of the char at the cursor
        pkt.cmd = PC_CMD_OP_READ | PC_CMD_AUTOINC;
        pkt.core = pmycore->core_id;
        pkt.reg = VGA_REG_CHAR;
        pkt.count = 8;   // char and all attributes
        // send the packet.  Report any errors
        txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + num requested
        if (txret != 0) {
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
        // Start timer to look for a read response.
        if (pctx->ptimer == 0)
            pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);
        // lock this resource to the UI session cn
        pslot->rsc[RSC_CHAR].uilock = (char) cn;
        // Nothing to send back to the user
        *plen = 0;
    }
    else if ((cmd == PCSET) && (rscid == RSC_CHAR)) {
        // The test to send is just the argument already in val[]
        charlen = strlen(val);
        if ((charlen > NUMCOL) || (charlen < 1)) {
            ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;
            return;
        }
        pctx->charlen = charlen;
        strcpy(pctx->charstr, val);
        sendstringtofpga(pctx, plen, buf);  // send string to display
    }
    if ((cmd == PCGET) && (rscid == RSC_CURSOR)) {
        // create a read packet to get the current cursor location
        pkt.cmd = PC_CMD_OP_READ | PC_CMD_AUTOINC;
        pkt.core = pmycore->core_id;
        pkt.reg = VGA_REG_CURCOL;
        pkt.count = 4;
        // send the packet.  Report any errors
        txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + num requested
        if (txret != 0) {
            ret = snprintf(buf, *plen, E_WRFPGA);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
        // Start timer to look for a read response.
        if (pctx->ptimer == 0)
            pctx->ptimer = add_timer(PC_ONESHOT, 100, noAck, (void *) pctx);
        // lock this resource to the UI session cn
        pslot->rsc[RSC_CURSOR].uilock = (char) cn;
        // Nothing to send back to the user
        *plen = 0;
    }
    else if ((cmd == PCSET) && (rscid == RSC_CURSOR)) {
        // get row, column, block/underline, and visible/invisible from user
        if (sscanf(val, "%d %d %c %c", &ccol, &crow, &style, &visible) != 4) {
            if ((crow > NUMROW) || (ccol > NUMCOL) || (crow < 1) || (ccol < 1) ||
                ((style != 'b') && (style != 'u')) ||
                ((visible != 'v') && (visible != 'i'))) {
                ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
                *plen = ret;
                return;
            }
        }
        pctx->currow = crow;
        pctx->curcol = ccol;
        pctx->curstyle = style;
        pctx->curvisible = visible;
        sendcursortofpga(pctx, plen, buf);  // send location, style
    }
    if ((cmd == PCGET) && (rscid == RSC_ATTR)) {
        ret = snprintf(buf, *plen, "%03x %03x %c %c\n", pctx->fgclr, pctx->bgclr, pctx->underline, pctx->blink);
        *plen = ret;  // (errors are handled in calling routine)
        return;
    }
    else if ((cmd == PCSET) && (rscid == RSC_ATTR)) {
        // get foreground/background color and whether or not to underline
        if (sscanf(val, "%x %x %c %c", &fg, &bg, &under, &blnk) != 4) {
            if (((under != 'u') && (under != 'n')) ||
                ((blnk  != 'b') && (blnk != 'n'))) {
                ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
                *plen = ret;
                return;
            }
        }
        pctx->fgclr = fg;
        pctx->bgclr = bg;
        pctx->underline = under;
        pctx->blink = blnk;
        sendattrtofpga(pctx, plen, buf);  // send location, style
    }
    else if ((cmd == PCSET) && (rscid == RSC_ROWOFF)) {
        // get foreground/background color and whether or not to underline
        if (sscanf(val, "%d", &newoff) != 1) {
            if ((newoff < 0) || (newoff >= NUMROW)) {
                ret = snprintf(buf, *plen,  E_BDVAL, pslot->rsc[rscid].name);
                *plen = ret;
                return;
            }
        }
        pctx->rowoff = newoff;
        sendcursortofpga(pctx, plen, buf);  // send location, style
    }

    return;
}


/**************************************************************
 * sendstringtofpga():  - Send string to character FIFO in FPGA
 * Put error messages into buf and update plen.
 **************************************************************/
static void sendstringtofpga(
    VGADEV *pctx,    // This peripheral's context
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)      // where to store user visible error messages
{
    PC_PKT   pkt;      // send write and read cmds to the vgaterm
    SLOT    *pslot;    // our slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK
    int      ret;      // generic return value


    pslot = pctx->pslot;
    pmycore = pslot->pcore;

    // create a write packet to set the value of the register
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_NOAUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = VGA_REG_CHAR;
    pkt.count = pctx->charlen;
    strncpy((char *)pkt.data, pctx->charstr, pctx->charlen);
  
    // try to send the packet.  Apply or release flow control.
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    if (txret != 0) {
        // the send of the new pin values did not succeed.  This
        // probably means the input buffer to the seruak port is full.
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
 * sendcursortofpga():  - Send cursor location and style to the
 * FPGA card.
 * Put error messages into buf and update plen.
 **************************************************************/
static void sendcursortofpga(
    VGADEV *pctx,    // This peripheral's context
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)      // where to store user visible error messages
{
    PC_PKT   pkt;      // send write and read cmds to the vgaterm
    SLOT    *pslot;    // our slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK
    int      ret;      // generic return value


    pslot = pctx->pslot;
    pmycore = pslot->pcore;

    // create a write packet to set the value of the register
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = VGA_REG_CURCOL;
    pkt.count = 4;
    pkt.data[0] = pctx->curcol - 1;
    pkt.data[1] = pctx->currow - 1;
    pkt.data[2] = pctx->rowoff;
    pkt.data[3] = (pctx->curstyle == 'b' ? 1 : 0) |
                  (pctx->curvisible == 'v' ? 1<< 1 : 0 );

    // try to send the packet.  Apply or release flow control.
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    if (txret != 0) {
        // the send of the new pin values did not succeed.  This
        // probably means the input buffer to the seruak port is full.
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
 * sendattrtofpga():  - Send colors and underline to FPGA
 * Put error messages into buf and update plen.
 **************************************************************/
static void sendattrtofpga(
    VGADEV *pctx,    // This peripheral's context
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)      // where to store user visible error messages
{
    PC_PKT   pkt;      // send write and read cmds to the vgaterm
    SLOT    *pslot;    // our slot info
    CORE    *pmycore;  // FPGA peripheral info
    int      txret;    // ==0 if the packet went out OK
    int      ret;      // generic return value


    pslot = pctx->pslot;
    pmycore = pslot->pcore;

    // create a write packet to set the value of the register
    pkt.cmd = PC_CMD_OP_WRITE | PC_CMD_AUTOINC;
    pkt.core = pmycore->core_id;
    pkt.reg = VGA_REG_FGRGB;
    pkt.count = 3;
    pkt.data[0] = pctx->fgclr;                     // rgb as 2/2/2
    pkt.data[1] = pctx->bgclr;                     // rgb as 2/2/2
    pkt.data[2] = ((pctx->underline == 'u') ? 0x01 : 0x00) |  // bit0=underline
                  ((pctx->blink == 'b') ? 0x02 : 0x00);       // bit1=blink
 
    // try to send the packet.  Apply or release flow control.
    txret = pc_tx_pkt(pmycore, &pkt, 4 + pkt.count); // 4 header + data

    if (txret != 0) {
        // the send of the new pin values did not succeed.  This
        // probably means the input buffer to the seruak port is full.
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
    void      *timer,   // handle of the timer that expired
    VGADEV    *pctx)    // Send pin values of this vgaterm to the FPGA
{
    // Log the missing ack
    pclog(E_NOACK);

    return;
}

// end of vgaterm.c


