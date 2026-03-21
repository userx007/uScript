#ifndef FT232H_UART_CONFIG_HPP
#define FT232H_UART_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              UART subcommand configurator                     //
//  FT232H — single interface in async serial mode               //
//  Note: UART and MPSSE modes are mutually exclusive on one     //
//  physical chip.  Use device=N to pair with an MPSSE module    //
//  on a different chip.                                          //
///////////////////////////////////////////////////////////////////

#define UART_COMMANDS_CONFIG_TABLE  \
UART_CMD_RECORD( open   )           \
UART_CMD_RECORD( close  )           \
UART_CMD_RECORD( cfg    )           \
UART_CMD_RECORD( write  )           \
UART_CMD_RECORD( read   )           \
UART_CMD_RECORD( script )           \
UART_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//              UART baud-rate presets                           //
///////////////////////////////////////////////////////////////////

#define UART_SPEED_CONFIG_TABLE              \
UART_SPEED_RECORD( "9600",      9600    )    \
UART_SPEED_RECORD( "19200",     19200   )    \
UART_SPEED_RECORD( "38400",     38400   )    \
UART_SPEED_RECORD( "57600",     57600   )    \
UART_SPEED_RECORD( "115200",    115200  )    \
UART_SPEED_RECORD( "230400",    230400  )    \
UART_SPEED_RECORD( "460800",    460800  )    \
UART_SPEED_RECORD( "921600",    921600  )

#endif // FT232H_UART_CONFIG_HPP
