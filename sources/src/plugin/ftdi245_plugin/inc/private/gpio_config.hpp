#pragma once

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  FT245 GPIO uses asynchronous bit-bang mode (BITMODE_BITBANG) //
//  to control all 8 data pins D0–D7 as a byte-wide GPIO port.  //
//                                                               //
//  Note: bit-bang and FIFO modes are mutually exclusive.        //
//  Close the GPIO driver before opening a FIFO driver on the   //
//  same device, and vice versa.                                 //
//                                                               //
//  Variants:                                                    //
//    FT245BM: bit-bang supported                                //
//    FT245R : bit-bang supported (also has CBUS bit-bang,       //
//             not covered by this driver)                       //
///////////////////////////////////////////////////////////////////

#define GPIO_COMMANDS_CONFIG_TABLE  \
GPIO_CMD_RECORD( open   )           \
GPIO_CMD_RECORD( close  )           \
GPIO_CMD_RECORD( cfg    )           \
GPIO_CMD_RECORD( dir    )           \
GPIO_CMD_RECORD( write  )           \
GPIO_CMD_RECORD( set    )           \
GPIO_CMD_RECORD( clear  )           \
GPIO_CMD_RECORD( toggle )           \
GPIO_CMD_RECORD( read   )           \
GPIO_CMD_RECORD( help   )
