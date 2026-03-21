#ifndef FT2232_GPIO_CONFIG_HPP
#define FT2232_GPIO_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  FT2232 GPIO exposes two 8-bit banks per MPSSE channel:       //
//    Low  bank  = ADBUS[7:0]  (SET/GET_BITS_LOW)                //
//    High bank  = ACBUS[7:0]  (SET/GET_BITS_HIGH)               //
//                                                               //
//  FT2232H: both channels A and B support MPSSE.                //
//  FT2232D: only channel A supports MPSSE.                      //
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

#endif // FT2232_GPIO_CONFIG_HPP
