/*
 * Name: daemon.h
 *
 * Description: This file contains the define's and data structures for use
 *              in the daemon part of pcdaemon
 *
 * Copyright:   Copyright (C) 2019 by Demand Peripherals, Inc.
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
 */

#ifndef DAEMON_H_
#define DAEMON_H_

#include "core.h"              // for the CORE definition


/***************************************************************************
 *  - Defines
 ***************************************************************************/
        // Types of resources.  Broadcast and is_writable are mutually exclusive
#define CAN_BROADCAST    1
#define IS_READABLE      2      /* can we issue a pcget to it? */
#define IS_WRITABLE      4      /* can we issue a pcset to it? */

        // Types of UI access */
#define PCGET            1
#define PCSET            2
#define PCCAT            3
#define PCLIST           4
#define PCLOAD           5

        // Different ways to register a fd for select
#define PC_READ          1
#define PC_WRITE         2
#define PC_EXCEPT        4

        // Max size of a line from the UI's
#define MXCMD        (2000)
        // Max size of a line to the UI's
#define MXRPLY       (1000)

        // Timer types for use in add_timer()
#define PC_UNUSED        0
#define PC_ONESHOT       1
#define PC_PERIODIC      2

        // Sizes of the slot array and number of resources per slot
#define MX_SLOT         25     /* maximum # plug-ins per daemon */
#define MX_RSC          10     /* maximum # resources per plugin */
#define MX_SONAME      200     /* maximum # of chars in plug-in file name */

        // Verbosity levels
#define PC_VERB_OFF      0     /* no verbose output at all */
#define PC_VERB_WARN     1     /* give errors and warnings */
#define PC_VERB_INFO     2     /* give normal progress output */
#define PC_VERB_TRACE    3     /* trace internal processing */

        // Default serial port to the FPGA
#define DEFFPGAPORT      "/dev/ttyUSB0"
#define DEFFPGABAUD      B115200


/***************************************************************************
 *  - Data structures  (please see design.txt for more explanation)
 ***************************************************************************/
typedef struct {
    char     *name;            // User visible name of the resource
    void    (*pgscb) ();       // Callback for get/set cmds from UI to plug-in
    void     *slot;            // Pointer to this resource's slot structure
    int       bkey;            // Broadcast key.  Broadcast sensor data if set
    int       uilock;          // UI ID # of session awaiting read/write reply
    int       flags;           // broadcast | readable | writeable flags
} RSC;

typedef struct {
    int       slot_id;         // zero indexed slot number for this slot
    char     *name;            // Human readable name of plug-in in slot
    char     *desc;            // Very brief description of plug-in
    char     *help;            // Full text of help for plug-in
    void     *handle;          // dlopen() handle for soname
    void     *priv;            // Pointer to plug-in's private data
    char      soname[MX_SONAME];// shared object file name
    RSC       rsc[MX_RSC];     // Resources visible to this slot
    CORE     *pcore;           // CORE pointer valid only if an FPGA peripheral
} SLOT;


/***************************************************************************
 *  - Forward references
 ***************************************************************************/

/***************************************************************************
 * getslotbyid(): - return a slot pointer given its index.
 *   This routine is used by plug-ins to help find what other
 * plug-ins are in the system.
 ***************************************************************************/
const SLOT * getslotbyid(
    int      id);

/***************************************************************************
 * add_fd(): - add a file descriptor to the select list
 ***************************************************************************/
void add_fd(
    int      fd,        // FD to add
    int      stype,     // OR of PC_READ, PC_WRITE, PC_EXCEPT
    void     (*scb) (), // select callback
    void    *pcb_data); // callback data 

/***************************************************************************
 * del_fd(): - delete a file descriptor from the select list
 ***************************************************************************/
void del_fd(
    int      fd);       // FD to delete


/***************************************************************************
 *  pclog():  Print an error message on stderr or to syslog
 ***************************************************************************/
void pclog(
    char *format, ...);   // printf format string


/***************************************************************************
 * add_timer(): - register a subroutine to be executed after a
 * specified number of milliseconds.  The callback must have two
 * input parameters.  The first is a pointer to a timer handle and
 * the second is a void pointer.  When called, the callback gets
 * the handle of the invoking timer as well as the private
 * void pointer that was registered with the callback.
 ***************************************************************************/
void        *add_timer(
    int      type,     // oneshot or periodic
    int      ms,       // milliseconds to timeout
    void   (*cb) (),   // timeout callback
    void    *pcb_data); // callback data

/***************************************************************************
 * del_timer(): - remove a timer from the queue.  The single
 * parameter is the void pointer returned when the timer was
 * added.
 ***************************************************************************/
void         del_timer(
    void    *ptimer);  // timer to delete


/***************************************************************************
 * bcst_ui(): - Broadcast the buffer down all UI connections that
 * have a matching monitor key.  Clear the key if there are no UIs
 * monitoring this resource any more.  Close UI sessions that fail
 * the write.
 ***************************************************************************/
void bcst_ui(
    char    *buf,        // buffer of chars to send
    int      len,        // number of chars to send
    int     *bkey);      // slot/rsc as an int

/***************************************************************************
 * send_ui(): - This routine is called to send data to the other
 * end of a UI connection.  Closes connection on error.
 ***************************************************************************/
void send_ui(
    char    *buf,        // buffer of chars to send
    int      len,        // number of chars to send
    int      cn);        // index to UI conn table

/***************************************************************************
 * prompt(): - Write a prompt out to the user.  A prompt indicates
 * the completion of the previous command.
 * Closes connection on error.
 ***************************************************************************/
void prompt(
    int      cn);        // index to UI conn table



/***************************************************************************
 *  - User visible error messages, strings, and printf formats
 ***************************************************************************/
#define E_BDCMD   "ERROR 001 : Unrecognized command: %s\n"
#define E_NOPERI  "ERROR 002 : Plug-in '%s' is not in system\n"
#define E_BDSLOT  "ERROR 003 : Unrecognized slot ID: %s\n"
#define E_NORSC   "ERROR 004 : No resource called '%s' in plug-in %s\n"
#define E_BUSY    "ERROR 005 : Resource '%s' is busy\n"
#define E_NREAD   "ERROR 006 : Resource '%s' is not readable\n"
#define E_NWRITE  "ERROR 007 : Resource '%s' is not writable\n"
#define E_BDVAL   "ERROR 008 : Invalid value given for resource '%s'\n"
#define E_NBUFF   "ERROR 009 : Would overflow buffer for resource '%s'\n"
#define LISTFORMAT "  %2d / %10s   %s\n"
#define LISTRSCFMT "                  - %s : %s%s%s\n"


/***************************************************************************
 *  - Log messages
 ***************************************************************************/
#define M_BADCONN     "Error accepting UI connection. errno=%d"
#define M_BADDRIVER   "plug-in initialization error for %s"
#define M_BADMLOCK    "Memory page locking failed with error: %s"
#define M_BADPORT     "configure of %s failed with: %s"
#define M_BADSCHED    "Scheduler changes failed with error: %s"
#define M_BADSLOT     "invalid shared object file: %s.  Ignoring request"
#define M_BADSO       "invalid shared object name: %s"
#define M_BADSYMB     "unable to load symbol %s in %s"
#define M_MISSTO      "Missed TO on %d.  Rescheduling"
#define M_NOCD        "chdir to / failed with error: %s"
#define M_NOFORK      "fork failed: %s"
#define M_NOMEM       "unable to allocate memory in %s"
#define M_NOMOREFD    "too many open file descriptors"
#define M_NONULL      "/dev/null open failed with error: %s"
#define M_NOOPEN      "open of %s failed with error: %s"
#define M_NOPORT      "open failed on port %s"
#define M_NOCORE      "open failed on FPGA binary file %s"
#define M_NOREAD      "read error on: %s"
#define M_NOREDIR     "cannot redirect %s to /dev/null"
#define M_NOSID       "setsid failed with error: %s"
#define M_NOSLOT      "No free slot for plugin: %s.  Ignoring request"
#define M_NOSO        "no plug-in loaded for slot %d"
#define M_NOUI        "No free UI sessions"



#endif /* DAEMON_H_ */


