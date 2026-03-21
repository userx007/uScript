#ifndef HYDRABUS_SPI_CONFIG_HPP
#define HYDRABUS_SPI_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//                  SPI subcommand configurator                  //
///////////////////////////////////////////////////////////////////

#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( cfg    )           \
SPI_CMD_RECORD( cs     )           \
SPI_CMD_RECORD( speed  )           \
SPI_CMD_RECORD( write  )           \
SPI_CMD_RECORD( read   )           \
SPI_CMD_RECORD( wrrd   )           \
SPI_CMD_RECORD( wrrdf  )           \
SPI_CMD_RECORD( script )           \
SPI_CMD_RECORD( aux    )           \
SPI_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//                  SPI speed configurator                       //
//  Matches HydraHAL::SPI::Speed enum values                     //
///////////////////////////////////////////////////////////////////

#define SPI_SPEED_CONFIG_TABLE      \
SPI_SPEED_RECORD( "320kHz",  0 )    \
SPI_SPEED_RECORD( "650kHz",  1 )    \
SPI_SPEED_RECORD( "1MHz",    2 )    \
SPI_SPEED_RECORD( "2MHz",    3 )    \
SPI_SPEED_RECORD( "5MHz",    4 )    \
SPI_SPEED_RECORD( "10MHz",   5 )    \
SPI_SPEED_RECORD( "21MHz",   6 )    \
SPI_SPEED_RECORD( "42MHz",   7 )

#endif // HYDRABUS_SPI_CONFIG_HPP
