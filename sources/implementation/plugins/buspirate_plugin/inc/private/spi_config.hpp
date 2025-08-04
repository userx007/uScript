///////////////////////////////////////////////////////////////////
//                    SPI subcommand configurator                //
///////////////////////////////////////////////////////////////////

#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( cfg   )            \
SPI_CMD_RECORD( cs    )            \
SPI_CMD_RECORD( per   )            \
SPI_CMD_RECORD( read  )            \
SPI_CMD_RECORD( sniff )            \
SPI_CMD_RECORD( speed )            \
SPI_CMD_RECORD( write )            \
SPI_CMD_RECORD( wrrd  )            \
SPI_CMD_RECORD( wrrdf )            \
SPI_CMD_RECORD( help  )            \

///////////////////////////////////////////////////////////////////
//                    SPI speed configurator                     //
///////////////////////////////////////////////////////////////////

#define SPI_SPEED_CONFIG_TABLE     \
SPI_SPEED_RECORD("30kHz"    ,0 )   \
SPI_SPEED_RECORD("125kHz"   ,1 )   \
SPI_SPEED_RECORD("250kHz"   ,2 )   \
SPI_SPEED_RECORD("1MHz"     ,3 )   \
SPI_SPEED_RECORD("2MHz"     ,4 )   \
SPI_SPEED_RECORD("2.6MHz"   ,5 )   \
SPI_SPEED_RECORD("4MHz"     ,6 )   \
SPI_SPEED_RECORD("8MHz"     ,7 )   \

