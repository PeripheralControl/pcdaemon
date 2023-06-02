/*
 * Name: drivlist.h
 *
 * Description: This file contains a table that tells for each peripheral
 *              what driver .so file to use, the driver ID number, how many
 *              FPGA pins the peripherals uses, and the direction (in/out) 
 *              of those pins.
 *
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
 */

#ifndef DRIVLIST_H_
#define DRIVLIST_H_


// Peripheral/Driver descriptions.  A list of driver names and the
// associated driver ID, Verilog source file, dirction of pin IO,
// and the number of IO pins the peripherals uses.
struct PDESC {
          // Internal name of the peripheral.  This is the name for the
          // .so loadable module and the name that appears in perilist.
    char *periname;

          // Driver ID.  The FPGA image has a table with the Driver IDs
          // for the peripheral in each slot/core of the FPGA build. 
          // This ID tells pcdaemon which shared object file to load for
          // the peripheral.  This table must match an equivalent one
          // in the pcdaemon code.
    int   drivid;

          // Name of the Verilog source file for this peripheral.
          // This name will disagree with periname when the peripheral
          // is an alias of an existing peripheral.  This lets us reuse
          // existing Verilog and change only the Linux driver.  The
          // touch4 is an example.  The underlying peripheral is just
          // four counters (count4) but the Linux loadable module 
          // interprets the counts in such a was as to detect touch
          // events.
    char *incname;

          // Pin dirs.  In theory we could make all pins inout but
          // some Verilog compilers (xst) have a bug that optimizes the
          // pins to a stuck at 1/0.  The fix is too give the invocation
          // of the peripheral a list of correct input or output declarations.
          // This is a bit field where 0 is an input and 1 is an output.
          // So in4 has a value of 0x0 while out4 has a value of 0xf.
          // Bidirectional lines are listed as outputs.  So, for example,
          // the gpio4 peripheral has directions value of 0xf.
          // The LSB corresponds to the lowest pin number.
    int   dirs;

          // Most peripherals use four pins, some eight, and some none.
          // The npins element tells how many pins the peripherals uses.
    int   npins;
};

struct PDESC pdesc[] = {
    // Note that these are the peripherals as made visible to the
    // enumerator.  For example, "avr" is, in hardware, an instance
    // of an espi peripheral, but we want to load the avr.so driver
    // so we alias "avr" to "espi".  This is the table of aliases,
    // or, if you will, the table of .so files.
 
    {"null", 1, "null", 0x0, 0 },
    {"serout8", 2, "serout", 0xff, 8 },
    {"rcc8", 3, "rcc8", 0xff, 8 },
    {"rcc4", 4, "rcc4", 0xf, 4 },
    {"ws2812", 5, "ws2812", 0xf, 4 },
    {"rcrx", 6, "rcrx", 0xe, 4 },
    {"serout4", 7, "serout", 0xf, 4 },
    {"dproten", 8, "dproten", 0x8, 4 },
    {"servo4", 9, "servo4", 0xf, 4 },
    {"stepu", 10, "stepu", 0xf, 4 },
    {"stepb", 11, "stepb", 0xf, 4 },
    {"pwmout4", 12, "pgen16", 0xf, 4 },
    {"quad2", 13, "quad2", 0x0, 4 },
    {"pwmin4", 14, "pwmin4", 0x0, 4 },
    {"ping4", 15, "ping4", 0xf, 4 },
    {"pgen16", 16, "pgen16", 0xf, 4 },
    {"irio", 17, "irio", 0x7, 4 },
    {"pulse2", 18, "pulse2", 0xf, 4 },
    {"touch4", 19, "count4", 0xf, 4 },
    {"dc2", 20, "dc2", 0xf, 4 },
    {"count4", 21, "count4", 0x0, 4 },
    {"gpio4", 22, "gpio4", 0xf, 4 },
    {"in4", 23, "in4", 0x0, 4 },
    {"out4", 24, "out4", 0xf, 4 },
    {"out4l", 25, "out4l", 0xf, 4 },
    {"dpespi", 26, "dpespi", 0x7, 4 },
    {"dpei2c", 27, "dpei2c", 0x7, 4 },
    {"dplcd6", 28, "dplcd6", 0xf, 4 },
    {"dpin32", 29, "dpin32", 0x7, 4 },
    {"dpio8", 30, "dpio8", 0x7, 4 },
    {"aamp", 31, "out4", 0xf, 4 },
    {"dpdac8", 32, "dpespi", 0x7, 4 },
    {"dpqpot", 33, "dpespi", 0x7, 4 },
    {"dprtc", 34, "dpespi", 0x7, 4 },
    {"dpavr", 35, "dpespi", 0x7, 4 },
    {"dpadc812", 36, "dpadc12", 0x7, 4 },
    {"dpslide4", 37, "dpadc12", 0x7, 4 },
    {"dptif", 38, "dptif", 0x7, 4 },
    {"dpus8", 39, "dpus8", 0x7, 4 },
    {"rfob", 40, "rfob", 0xc, 4 },
    {"dpout32", 41, "dpout32", 0xf, 4 },
    {"bb4io", 42, "bb4io", 0x0, 0 },
    {"axo2", 43, "axo2", 0x0, 0 },
    {"tang4k", 44, "tang4k", 0x0, 0 },
    {"tonegen", 45, "tonegen", 0xf, 4 },
    {"stpxo2", 46, "stpxo2", 0x0, 0 },
    {"basys3", 47, "basys3", 0x0, 0 },
    {"cvcc", 48, "cvcc", 0x1, 4 },
    {"vgaterm", 49, "vgaterm", 0xff, 8 },
    {"dgspi", 50, "dgspi", 0xb, 4 },
    {"sndgen", 51, "sndgen", 0xf, 4 },
    {"ps2", 52, "ps2", 0xf, 4 },
};

#define NPERI (sizeof(pdesc) / sizeof(struct PDESC))

#endif // DRIVLIST_H_
