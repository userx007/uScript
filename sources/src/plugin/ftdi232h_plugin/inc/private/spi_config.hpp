#ifndef FT232H_SPI_CONFIG_HPP
#define FT232H_SPI_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              SPI subcommand configurator                      //
///////////////////////////////////////////////////////////////////

#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( open   )           \
SPI_CMD_RECORD( close  )           \
SPI_CMD_RECORD( cfg    )           \
SPI_CMD_RECORD( cs     )           \
SPI_CMD_RECORD( write  )           \
SPI_CMD_RECORD( read   )           \
SPI_CMD_RECORD( wrrd   )           \
SPI_CMD_RECORD( wrrdf  )           \
SPI_CMD_RECORD( xfer   )           \
SPI_CMD_RECORD( script )           \
SPI_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//              SPI clock presets                                 //
//  FT232H: 60 MHz base, max SCK 30 MHz                          //
///////////////////////////////////////////////////////////////////

#define SPI_SPEED_CONFIG_TABLE             \
SPI_SPEED_RECORD( "100kHz",   100000 )    \
SPI_SPEED_RECORD( "500kHz",   500000 )    \
SPI_SPEED_RECORD( "1MHz",    1000000 )    \
SPI_SPEED_RECORD( "2MHz",    2000000 )    \
SPI_SPEED_RECORD( "5MHz",    5000000 )    \
SPI_SPEED_RECORD( "10MHz",  10000000 )    \
SPI_SPEED_RECORD( "20MHz",  20000000 )    \
SPI_SPEED_RECORD( "30MHz",  30000000 )

#endif // FT232H_SPI_CONFIG_HPP
