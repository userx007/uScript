///////////////////////////////////////////////////////////////////
//                    RAWWIRE subcommand configurator            //
///////////////////////////////////////////////////////////////////


#define RAWWIRE_COMMANDS_CONFIG_TABLE  \
RAWWIRE_CMD_RECORD( bit   )            \
RAWWIRE_CMD_RECORD( cfg   )            \
RAWWIRE_CMD_RECORD( clock )            \
RAWWIRE_CMD_RECORD( cs    )            \
RAWWIRE_CMD_RECORD( data  )            \
RAWWIRE_CMD_RECORD( per   )            \
RAWWIRE_CMD_RECORD( pic   )            \
RAWWIRE_CMD_RECORD( read  )            \
RAWWIRE_CMD_RECORD( speed )            \
RAWWIRE_CMD_RECORD( write )            \


///////////////////////////////////////////////////////////////////
//                    UART speed configurator                    //
///////////////////////////////////////////////////////////////////

#define RAWWIRE_SPEED_CONFIG_TABLE    \
RAWWIRE_SPEED_RECORD("5KHz"  , 0)     \
RAWWIRE_SPEED_RECORD("50kHz" , 1)     \
RAWWIRE_SPEED_RECORD("100kHz", 2)     \
RAWWIRE_SPEED_RECORD("400kHz", 3)     \

