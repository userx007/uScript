///////////////////////////////////////////////////////////////////
//                    MODE subcommand configurator                //
///////////////////////////////////////////////////////////////////

/* commands with two parameters */
#define MODE_COMMANDS_CONFIG_TABLE           \
MODE_CMD_RECORD( bin,   0x00, 20, BBIO1 )    \
MODE_CMD_RECORD( reset, 0x0F, 1,  -     )    \
MODE_CMD_RECORD( spi,   0x01, 1,  SPI1  )    \
MODE_CMD_RECORD( i2c,   0x02, 1,  I2C1  )    \
MODE_CMD_RECORD( uart,  0x03, 1,  ART1  )    \
MODE_CMD_RECORD( 1wire, 0x04, 1,  1W01  )    \
MODE_CMD_RECORD( rawire,0x05, 1,  RAW1  )    \
MODE_CMD_RECORD( jtag,  0x06, 1,  JTG1  )    \
MODE_CMD_RECORD( exit,  0x00, 1,  BBI01 )    \

