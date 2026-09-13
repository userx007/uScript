#ifndef HYDRABUS_I2C_CONFIG_HPP
#define HYDRABUS_I2C_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//                  I2C subcommand configurator                  //
///////////////////////////////////////////////////////////////////

#define I2C_COMMANDS_CONFIG_TABLE  \
I2C_CMD_RECORD( cfg     )          \
I2C_CMD_RECORD( speed   )          \
I2C_CMD_RECORD( bit     )          \
I2C_CMD_RECORD( write   )          \
I2C_CMD_RECORD( read    )          \
I2C_CMD_RECORD( wrrd    )          \
I2C_CMD_RECORD( wrrdf   )          \
I2C_CMD_RECORD( scan    )          \
I2C_CMD_RECORD( stretch )          \
I2C_CMD_RECORD( script )           \
I2C_CMD_RECORD( aux     )          \
I2C_CMD_RECORD( help    )

///////////////////////////////////////////////////////////////////
//                  I2C speed configurator                       //
//  Matches HydraHAL::I2C::Speed enum values                     //
///////////////////////////////////////////////////////////////////

#define I2C_SPEED_CONFIG_TABLE      \
I2C_SPEED_RECORD( "50kHz",   0 )    \
I2C_SPEED_RECORD( "100kHz",  1 )    \
I2C_SPEED_RECORD( "400kHz",  2 )    \
I2C_SPEED_RECORD( "1MHz",    3 )

#endif // HYDRABUS_I2C_CONFIG_HPP