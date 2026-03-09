#pragma once

///////////////////////////////////////////////////////////////////
//              I2C subcommand configurator                      //
///////////////////////////////////////////////////////////////////

#define I2C_COMMANDS_CONFIG_TABLE  \
I2C_CMD_RECORD( open   )           \
I2C_CMD_RECORD( close  )           \
I2C_CMD_RECORD( cfg    )           \
I2C_CMD_RECORD( write  )           \
I2C_CMD_RECORD( read   )           \
I2C_CMD_RECORD( wrrd   )           \
I2C_CMD_RECORD( wrrdf  )           \
I2C_CMD_RECORD( scan   )           \
I2C_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//              I2C clock presets                                //
//  CP2112 supports any raw Hz value; list the standard speeds.  //
///////////////////////////////////////////////////////////////////

#define I2C_SPEED_CONFIG_TABLE               \
I2C_SPEED_RECORD( "10kHz",    10000  )      \
I2C_SPEED_RECORD( "100kHz",  100000  )      \
I2C_SPEED_RECORD( "400kHz",  400000  )
