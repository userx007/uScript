#pragma once

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
//  CH347 SPI: 468.75 kHz – 60 MHz                               //
///////////////////////////////////////////////////////////////////

#define SPI_SPEED_CONFIG_TABLE              \
SPI_SPEED_RECORD( "468kHz",   468750  )    \
SPI_SPEED_RECORD( "937kHz",   937500  )    \
SPI_SPEED_RECORD( "1.875MHz", 1875000 )    \
SPI_SPEED_RECORD( "3.75MHz",  3750000 )    \
SPI_SPEED_RECORD( "7.5MHz",   7500000 )    \
SPI_SPEED_RECORD( "15MHz",   15000000 )    \
SPI_SPEED_RECORD( "30MHz",   30000000 )    \
SPI_SPEED_RECORD( "60MHz",   60000000 )
