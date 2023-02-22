/*
 *  Name: isl29125.c
 *
 *  Description: Interface to ISL29125 RGB sensor
 *
 *  Resources:
 *    bus-      Path to the I2C bus for the ISL29125 (/dev/i2c-0)
 *    period -  Update interval in milliseconds
 *    colors -  RGB sensor data as three hex numbers    
 */

/*
 * Copyright:   Copyright (C) 2022 by Demand Peripherals, Inc.
 *
 * License:     This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License as 
 *              published by the Free Software Foundation, either version 3 of 
 *              the License, or (at your option) any later version.
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *              GNU General Public License for more details. 
 *
 *              You should have received a copy of the GNU General Public License
 *              along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <limits.h>             // for PATH_MAX
#include <linux/i2c-dev.h>
#include "daemon.h"
#include "readme.h"


/**************************************************************
 *  - Limits and defines
 **************************************************************/
        // resource names and numbers
#define FN_BUS             "bus"
#define FN_PERIOD          "period"
#define FN_COLORS          "colors"
#define RSC_BUS            0
#define RSC_PERIOD         1
#define RSC_COLORS         2
        // What we are is a ...
#define PLUGIN_NAME        "isl29125"
        // I2C bus address for the ISL29125
#define ISL_I2C_ADDR       0x44
#define GETCOUNT           15
        // Maximum size of output string
#define MX_MSGLEN          120


/**************************************************************
 *  - Data structures
 **************************************************************/
    // All state info for an instance of a isl29125
typedef struct
{
    void    *pslot;             // handle to plug-in's's slot info
    void    *ptimer;            // poll timer
    int      bus;               // I2C bus number
    int      period;            // update period for measurement poll
    int      islfd;             // File Descriptor (=-1 if closed)
} ISL125;


/**************************************************************
 *  - Function prototypes
 **************************************************************/
static void usercmd(int, int, char *, SLOT *, int, int *, char *);
static void colorscb(void *, ISL125 *);
void get_islfd(ISL125 *);


/**************************************************************
 * Initialize():  - Allocate our permanent storage and set up
 * the read/write callbacks.
 **************************************************************/
int Initialize(
    SLOT *pslot)       // points to the SLOT for this plug-in
{
    ISL125   *pctx;    // our local bus context

    // Allocate memory for this plug-in
    pctx = (ISL125 *) malloc(sizeof(ISL125));
    if (pctx == (ISL125 *) 0) {
        // Malloc failure this early?
        pclog("memory allocation failure in isl29125 initialization");
        return (-1);
    }

    // Init our private structure
    pctx->pslot  = pslot;       // this instance of the hello demo
    pctx->period = 0;           // default period turns off the sensor
    pctx->bus    = 0;           // bus #0 is the default
    pctx->islfd  = -1;          // no FD to start
    pctx->ptimer = (void *) 0;  // no polling at start

    // Register name and private data
    pslot->name = "isl29125";
    pslot->priv = pctx;
    pslot->desc = "ISL29125 RGB color sensor";
    pslot->help = README;

    // Add handlers for the user visible resources
    pslot->rsc[RSC_BUS].name = FN_BUS;
    pslot->rsc[RSC_BUS].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_BUS].bkey = 0;
    pslot->rsc[RSC_BUS].pgscb = usercmd;
    pslot->rsc[RSC_BUS].uilock = -1;
    pslot->rsc[RSC_BUS].slot = pslot;
    pslot->rsc[RSC_PERIOD].name = FN_PERIOD;
    pslot->rsc[RSC_PERIOD].flags = IS_READABLE | IS_WRITABLE;
    pslot->rsc[RSC_PERIOD].bkey = 0;
    pslot->rsc[RSC_PERIOD].pgscb = usercmd;
    pslot->rsc[RSC_PERIOD].uilock = -1;
    pslot->rsc[RSC_PERIOD].slot = pslot;
    pslot->rsc[RSC_COLORS].name = FN_COLORS;
    pslot->rsc[RSC_COLORS].flags = CAN_BROADCAST;
    pslot->rsc[RSC_COLORS].bkey = 0;
    pslot->rsc[RSC_COLORS].pgscb = 0;
    pslot->rsc[RSC_COLORS].uilock = -1;
    pslot->rsc[RSC_COLORS].slot = pslot;

    return (0);
}


/**************************************************************
 * usercmd():  - The user is reading or setting one of the configurable
 * resources. 
 **************************************************************/
void usercmd(
    int      cmd,      //==PCGET if a read, ==PCSET on write
    int      rscid,    // ID of resource being accessed
    char    *val,      // new value for the resource
    SLOT    *pslot,    // pointer to slot info.
    int      cn,       // Index into UI table for requesting conn
    int     *plen,     // size of buf on input, #char in buf on output
    char    *buf)
{
    ISL125    *pctx;     // our local info
    int        ret;      // return count
    int        nbus;     // new bus value
    int        nperiod;  // new poll period value

    // point to the current context
    pctx = (ISL125 *) pslot->priv;

    // Handle getting and setting the bus number and poll period
    if ((cmd == PCGET) && (rscid == RSC_BUS)) {
        ret = snprintf(buf, *plen, "%d\n", pctx->bus);
        *plen = ret;  // (errors are handled in calling routine)
    }
    else if ((cmd == PCSET) && (rscid == RSC_BUS)) {
        ret = sscanf(val, "%d", &nbus);
        if ((ret != 1) || (nbus < 0) || (nbus > 20)) {  // 20 I2c busses ??
            ret = snprintf(buf, *plen, E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
        pctx->bus = nbus;  // record the new value

        // open or reopen the I2C bus device
        get_islfd(pctx);
    }
    else if ((cmd == PCGET) && (rscid == RSC_PERIOD)) {
        ret = snprintf(buf, *plen, "%d\n", pctx->period);
        *plen = ret;  // (errors are handled in calling routine)
    }
    else if ((cmd == PCSET) && (rscid == RSC_PERIOD)) {
        ret = sscanf(val, "%d", &nperiod);
        if ((ret != 1) || (nperiod < 0) || (nperiod > 5000)) {
            ret = snprintf(buf, *plen, E_BDVAL, pslot->rsc[rscid].name);
            *plen = ret;  // (errors are handled in calling routine)
            return;
        }
        pctx->period = nperiod; // record the new value

        // delete old timer and create a new one with the new period
        if (pctx->ptimer) {
            del_timer(pctx->ptimer);
        }
        if (pctx->period != 0) {
            pctx->ptimer = add_timer(PC_PERIODIC, pctx->period, colorscb, (void *) pctx);
        }
    }
    return;
}


/***************************************************************************
 *  islreadcb()  - poll and process data from the isl29125
 *
 ***************************************************************************/
void colorscb(
    void      *timer,   // handle of the timer that expired
    ISL125    *pctx)    // Send message to broadcast resource
{
    SLOT     *pslot;
    RSC      *prsc;     // pointer to this slot's counts resource
    char      i2cout[GETCOUNT];  // put I2C send data here
    char      i2cin[GETCOUNT];   // put I2C response here
    int       rcount;   // number of bytes read from I2C device
    char      lineout[MX_MSGLEN];  // output to send to users
    int       nout;     // length of output line

    // Get slot and pointer to colors resource structure
    pslot = pctx->pslot;
    prsc = &(pslot->rsc[RSC_COLORS]);

    // Set the initial register for the read
    i2cout[0] = 0;    // read from reg #0
    if (write(pctx->islfd, i2cout, 1) < 1) {
        pclog("Failed to set ISL29125 read register");
        // other error processing???
    }

    // Read the I2C data
    rcount = read(pctx->islfd, i2cin, GETCOUNT);
    if (rcount < 0) {
        if (errno == EAGAIN)
            return;      // return to try again later
        // Not much we can do at this point.  Close and log it.
        close(pctx->islfd);
        del_fd(pctx->islfd);
        pctx->islfd = -1;
        del_timer(pctx->ptimer);
        pctx->ptimer = (void *) 0;
        pclog("Error reading I2C device.  Device disabled");
        return;
    }

    // Sanity check should have the device ID (0x7d) in first byte
    if (i2cin[0] != 0x7d) {
        pclog("Error reading I2C device.  Retrying ...");
        return;
    }

    // broadcast the color values if anyone is listening
    if (prsc->bkey) {
        nout = snprintf(lineout, MX_MSGLEN, "%04x %04x %04x\n",
                        (i2cin[12] << 8) + i2cin[11],   // red
                        (i2cin[10] << 8) + i2cin[9],    // green
                        (i2cin[14] << 8) + i2cin[13]);  // blue
        // bkey will return cleared if UIs are no longer monitoring us
        bcst_ui(lineout, nout, &(prsc->bkey));
    }        

    return;
}


/***************************************************************************
 *  get_islfd()  - open or reopen the FD to the I2C bus
 *
 ***************************************************************************/
void get_islfd(ISL125 *pctx)
{
    char      devstr[PATH_MAX];  // path to /dev/i2c-X
    uint8_t   i2cbuf[GETCOUNT];  // buffer to send to isl29125

    // close FD and remove callback if already open
    if (pctx->islfd >= 0) {
        del_fd(pctx->islfd);
        close(pctx->islfd);
        pctx->islfd = -1;
    }

    // open and configure a new FD
    (void) snprintf(devstr, PATH_MAX, "/dev/i2c-%d", pctx->bus);
    if ((pctx->islfd = open(devstr, O_RDWR)) < 0) {
        pclog("I2C bus could not be opened for read/write.  Permissions?");
        return;
    }
    if (ioctl(pctx->islfd, I2C_SLAVE, ISL_I2C_ADDR) < 0) {
        close(pctx->islfd);
        pctx->islfd = -1;
        pclog("ISL29125 not found on I2C bus.");
        return;
    }

    // The low 3 bits of register 1 are "mode" bits for the colors.
    // Set this value to 5 to enable all colors.  We don't use interrupts
    // so that is left unconfigured and we use the default IR compensation
    i2cbuf[0] = 1;    // the register
    i2cbuf[1] = 5;    // the mode bits
    if (write(pctx->islfd, i2cbuf, 2) < 2) {
        pclog("Config write to ISL29125 failed");
        // other error processing???
    }
}
 

