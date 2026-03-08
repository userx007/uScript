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
//              I2C clock presets                                 //
//  Feed generic_module_set_speed → setModuleSpeed(I2C, Hz)      //
///////////////////////////////////////////////////////////////////

#define I2C_SPEED_CONFIG_TABLE             \
I2C_SPEED_RECORD( "50kHz",    50000  )    \
I2C_SPEED_RECORD( "100kHz",   100000 )    \
I2C_SPEED_RECORD( "400kHz",   400000 )    \
I2C_SPEED_RECORD( "1MHz",    1000000 )
