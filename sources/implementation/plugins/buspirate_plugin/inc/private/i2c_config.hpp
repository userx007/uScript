///////////////////////////////////////////////////////////////////
//                    I2C subcommand configurator                //
///////////////////////////////////////////////////////////////////

#define I2C_COMMANDS_CONFIG_TABLE  \
I2C_CMD_RECORD( mode  )            \
I2C_CMD_RECORD( aux   )            \
I2C_CMD_RECORD( bit   )            \
I2C_CMD_RECORD( per   )            \
I2C_CMD_RECORD( read  )            \
I2C_CMD_RECORD( sniff )            \
I2C_CMD_RECORD( speed )            \
I2C_CMD_RECORD( write )            \
I2C_CMD_RECORD( wrrd  )            \
I2C_CMD_RECORD( wrrdf )            \
I2C_CMD_RECORD( help  )            \

///////////////////////////////////////////////////////////////////
//                    I2C speed configurator                     //
///////////////////////////////////////////////////////////////////

#define I2C_SPEED_CONFIG_TABLE    \
I2C_SPEED_RECORD("5KHz"    ,0 )   \
I2C_SPEED_RECORD("50kHz"   ,1 )   \
I2C_SPEED_RECORD("100kHz"  ,2 )   \
I2C_SPEED_RECORD("400kHz"  ,3 )   \

