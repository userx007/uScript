#pragma once

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  FT4232H GPIO exposes two 8-bit banks per MPSSE channel:      //
//    Low  bank  = ADBUS[7:0]                                    //
//    High bank  = ACBUS[7:0]                                    //
//                                                               //
//  All commands that target a bank take "low" or "high" as      //
//  their first argument.                                        //
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
