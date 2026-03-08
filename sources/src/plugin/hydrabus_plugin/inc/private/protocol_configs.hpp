#pragma once

///////////////////////////////////////////////////////////////////
//                  UART subcommand configurator                 //
///////////////////////////////////////////////////////////////////

#define UART_COMMANDS_CONFIG_TABLE  \
UART_CMD_RECORD( baud   )           \
UART_CMD_RECORD( parity )           \
UART_CMD_RECORD( echo   )           \
UART_CMD_RECORD( bridge )           \
UART_CMD_RECORD( write  )           \
UART_CMD_RECORD( read   )           \
UART_CMD_RECORD( aux    )           \
UART_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//                  OneWire subcommand configurator              //
///////////////////////////////////////////////////////////////////

#define ONEWIRE_COMMANDS_CONFIG_TABLE  \
ONEWIRE_CMD_RECORD( cfg    )           \
ONEWIRE_CMD_RECORD( reset  )           \
ONEWIRE_CMD_RECORD( write  )           \
ONEWIRE_CMD_RECORD( read   )           \
ONEWIRE_CMD_RECORD( swio   )           \
ONEWIRE_CMD_RECORD( aux    )           \
ONEWIRE_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//                  RawWire subcommand configurator              //
///////////////////////////////////////////////////////////////////

#define RAWWIRE_COMMANDS_CONFIG_TABLE  \
RAWWIRE_CMD_RECORD( cfg    )           \
RAWWIRE_CMD_RECORD( speed  )           \
RAWWIRE_CMD_RECORD( sda    )           \
RAWWIRE_CMD_RECORD( clk    )           \
RAWWIRE_CMD_RECORD( bit    )           \
RAWWIRE_CMD_RECORD( ticks  )           \
RAWWIRE_CMD_RECORD( write  )           \
RAWWIRE_CMD_RECORD( read   )           \
RAWWIRE_CMD_RECORD( aux    )           \
RAWWIRE_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//                  RawWire speed configurator                   //
///////////////////////////////////////////////////////////////////

#define RAWWIRE_SPEED_CONFIG_TABLE      \
RAWWIRE_SPEED_RECORD( "5kHz",    0 )    \
RAWWIRE_SPEED_RECORD( "50kHz",   1 )    \
RAWWIRE_SPEED_RECORD( "100kHz",  2 )    \
RAWWIRE_SPEED_RECORD( "1MHz",    3 )

///////////////////////////////////////////////////////////////////
//                  SWD subcommand configurator                  //
///////////////////////////////////////////////////////////////////

#define SWD_COMMANDS_CONFIG_TABLE  \
SWD_CMD_RECORD( init      )        \
SWD_CMD_RECORD( multidrop )        \
SWD_CMD_RECORD( read_dp   )        \
SWD_CMD_RECORD( write_dp  )        \
SWD_CMD_RECORD( read_ap   )        \
SWD_CMD_RECORD( write_ap  )        \
SWD_CMD_RECORD( scan      )        \
SWD_CMD_RECORD( abort     )        \
SWD_CMD_RECORD( help      )

///////////////////////////////////////////////////////////////////
//                  Smartcard subcommand configurator            //
///////////////////////////////////////////////////////////////////

#define SMARTCARD_COMMANDS_CONFIG_TABLE  \
SMARTCARD_CMD_RECORD( cfg       )        \
SMARTCARD_CMD_RECORD( rst       )        \
SMARTCARD_CMD_RECORD( baud      )        \
SMARTCARD_CMD_RECORD( prescaler )        \
SMARTCARD_CMD_RECORD( guardtime )        \
SMARTCARD_CMD_RECORD( write     )        \
SMARTCARD_CMD_RECORD( read      )        \
SMARTCARD_CMD_RECORD( atr       )        \
SMARTCARD_CMD_RECORD( aux       )        \
SMARTCARD_CMD_RECORD( help      )

///////////////////////////////////////////////////////////////////
//                  NFC subcommand configurator                  //
///////////////////////////////////////////////////////////////////

#define NFC_COMMANDS_CONFIG_TABLE  \
NFC_CMD_RECORD( mode      )        \
NFC_CMD_RECORD( rf        )        \
NFC_CMD_RECORD( write     )        \
NFC_CMD_RECORD( write_bits)        \
NFC_CMD_RECORD( aux       )        \
NFC_CMD_RECORD( help      )

///////////////////////////////////////////////////////////////////
//                  MMC subcommand configurator                  //
///////////////////////////////////////////////////////////////////

#define MMC_COMMANDS_CONFIG_TABLE  \
MMC_CMD_RECORD( cfg     )          \
MMC_CMD_RECORD( cid     )          \
MMC_CMD_RECORD( csd     )          \
MMC_CMD_RECORD( ext_csd )          \
MMC_CMD_RECORD( read    )          \
MMC_CMD_RECORD( write   )          \
MMC_CMD_RECORD( aux     )          \
MMC_CMD_RECORD( help    )

///////////////////////////////////////////////////////////////////
//                  SDIO subcommand configurator                 //
///////////////////////////////////////////////////////////////////

#define SDIO_COMMANDS_CONFIG_TABLE  \
SDIO_CMD_RECORD( cfg       )        \
SDIO_CMD_RECORD( send_no   )        \
SDIO_CMD_RECORD( send_short)        \
SDIO_CMD_RECORD( send_long )        \
SDIO_CMD_RECORD( read      )        \
SDIO_CMD_RECORD( write     )        \
SDIO_CMD_RECORD( aux       )        \
SDIO_CMD_RECORD( help      )
