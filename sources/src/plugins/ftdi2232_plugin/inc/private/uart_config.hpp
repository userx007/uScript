#ifndef FT2232_UART_CONFIG_HPP
#define FT2232_UART_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              UART subcommand configurator                     //
//  FT2232D only — channel B is the async serial interface       //
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

#endif // FT2232_UART_CONFIG_HPP