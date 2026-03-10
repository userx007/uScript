#pragma once

///////////////////////////////////////////////////////////////////
//              UART subcommand configurator                     //
//                                                               //
//  FT4232H channels C and D are async UART.                    //
//  Max baud rate: ~3 Mbps (60 MHz / 20).                       //
//                                                               //
//  The UART driver (FT4232HUART) wraps FTD2XX channel C/D      //
//  operated in VCP (Virtual COM Port) or D2XX async mode.      //
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
//              UART baud rate presets                            //
///////////////////////////////////////////////////////////////////

#define UART_SPEED_CONFIG_TABLE             \
UART_SPEED_RECORD( "9600",      9600   )   \
UART_SPEED_RECORD( "19200",    19200   )   \
UART_SPEED_RECORD( "38400",    38400   )   \
UART_SPEED_RECORD( "57600",    57600   )   \
UART_SPEED_RECORD( "115200",  115200   )   \
UART_SPEED_RECORD( "230400",  230400   )   \
UART_SPEED_RECORD( "460800",  460800   )   \
UART_SPEED_RECORD( "921600",  921600   )   \
UART_SPEED_RECORD( "1M",     1000000   )   \
UART_SPEED_RECORD( "3M",     3000000   )
