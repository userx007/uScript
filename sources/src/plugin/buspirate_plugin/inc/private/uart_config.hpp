///////////////////////////////////////////////////////////////////
//                    UART subcommand configurator               //
///////////////////////////////////////////////////////////////////

#define UART_COMMANDS_CONFIG_TABLE  \
UART_CMD_RECORD( bdr   )            \
UART_CMD_RECORD( cfg   )            \
UART_CMD_RECORD( echo  )            \
UART_CMD_RECORD( mode  )            \
UART_CMD_RECORD( per   )            \
UART_CMD_RECORD( speed )            \
UART_CMD_RECORD( write )            \
UART_CMD_RECORD( help  )            \

///////////////////////////////////////////////////////////////////
//                    UART speed configurator                    //
///////////////////////////////////////////////////////////////////

#define UART_SPEED_CONFIG_TABLE    \
UART_SPEED_RECORD("300"    , 0)    \
UART_SPEED_RECORD("1200"   , 1)    \
UART_SPEED_RECORD("2400"   , 2)    \
UART_SPEED_RECORD("4800"   , 3)    \
UART_SPEED_RECORD("9600"   , 4)    \
UART_SPEED_RECORD("19200"  , 5)    \
UART_SPEED_RECORD("31250"  , 6)    \
UART_SPEED_RECORD("38400"  , 7)    \
UART_SPEED_RECORD("57600"  , 8)    \
UART_SPEED_RECORD("115200" ,10)    \


