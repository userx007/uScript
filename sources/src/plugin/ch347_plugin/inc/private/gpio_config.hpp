#pragma once

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  CH347 exposes GPIO0–GPIO7 (8 pins), each independently       //
//  configurable as input or output.                             //
///////////////////////////////////////////////////////////////////

#define GPIO_COMMANDS_CONFIG_TABLE  \
GPIO_CMD_RECORD( open   )           \
GPIO_CMD_RECORD( close  )           \
GPIO_CMD_RECORD( dir    )           \
GPIO_CMD_RECORD( write  )           \
GPIO_CMD_RECORD( set    )           \
GPIO_CMD_RECORD( clear  )           \
GPIO_CMD_RECORD( toggle )           \
GPIO_CMD_RECORD( read   )           \
GPIO_CMD_RECORD( help   )
