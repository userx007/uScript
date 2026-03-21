#ifndef FT232H_GPIO_CONFIG_HPP
#define FT232H_GPIO_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  FT232H GPIO banks per MPSSE channel:                         //
//    Low  bank  = ADBUS[7:0]                                    //
//    High bank  = ACBUS[7:0]                                    //
///////////////////////////////////////////////////////////////////

#define GPIO_COMMANDS_CONFIG_TABLE  \
GPIO_CMD_RECORD( open   )           \
GPIO_CMD_RECORD( close  )           \
GPIO_CMD_RECORD( cfg    )           \
GPIO_CMD_RECORD( dir    )           \
GPIO_CMD_RECORD( write  )           \
GPIO_CMD_RECORD( set    )           \
GPIO_CMD_RECORD( clear  )           \
GPIO_CMD_RECORD( toggle )           \
GPIO_CMD_RECORD( read   )           \
GPIO_CMD_RECORD( help   )

#endif // FT232H_GPIO_CONFIG_HPP
