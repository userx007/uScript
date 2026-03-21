#ifndef CH374_JTAG_CONFIG_HPP
#define CH374_JTAG_CONFIG_HPP


///////////////////////////////////////////////////////////////////
//              JTAG subcommand configurator                     //
///////////////////////////////////////////////////////////////////

#define JTAG_COMMANDS_CONFIG_TABLE  \
JTAG_CMD_RECORD( open   )           \
JTAG_CMD_RECORD( close  )           \
JTAG_CMD_RECORD( cfg    )           \
JTAG_CMD_RECORD( reset  )           \
JTAG_CMD_RECORD( write  )           \
JTAG_CMD_RECORD( read   )           \
JTAG_CMD_RECORD( wrrd   )           \
JTAG_CMD_RECORD( script )           \
JTAG_CMD_RECORD( help   )

///////////////////////////////////////////////////////////////////
//              JTAG clock-rate presets (0=slowest, 5=fastest)  //
///////////////////////////////////////////////////////////////////

#define JTAG_RATE_CONFIG_TABLE          \
JTAG_RATE_RECORD( "rate0", 0 )         \
JTAG_RATE_RECORD( "rate1", 1 )         \
JTAG_RATE_RECORD( "rate2", 2 )         \
JTAG_RATE_RECORD( "rate3", 3 )         \
JTAG_RATE_RECORD( "rate4", 4 )         \
JTAG_RATE_RECORD( "rate5", 5 )

#endif // CH374_JTAG_CONFIG_HPP