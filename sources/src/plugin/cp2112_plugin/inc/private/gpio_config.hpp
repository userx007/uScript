#ifndef CP2112_GPIO_CONFIG_HPP
#define CP2112_GPIO_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              GPIO subcommand configurator                     //
//                                                               //
//  CP2112 GPIO: single bank of 8 pins (GPIO.0 – GPIO.7).       //
//  No "low/high bank" concept — all operations address the      //
//  8-pin port as a flat byte mask.                              //
//                                                               //
//  Special-function pins (AN495 §5.2):                         //
//    GPIO.0  — TX LED       (specialFuncMask bit 0)            //
//    GPIO.1  — interrupt    (specialFuncMask bit 1)            //
//    GPIO.6  — clock output (specialFuncMask bit 6)            //
//    GPIO.7  — RX LED       (specialFuncMask bit 7)            //
///////////////////////////////////////////////////////////////////

#define GPIO_COMMANDS_CONFIG_TABLE  \
GPIO_CMD_RECORD( open   )           \
GPIO_CMD_RECORD( close  )           \
GPIO_CMD_RECORD( cfg    )           \
GPIO_CMD_RECORD( write  )           \
GPIO_CMD_RECORD( set    )           \
GPIO_CMD_RECORD( clear  )           \
GPIO_CMD_RECORD( read   )           \
GPIO_CMD_RECORD( help   )

#endif // CP2112_GPIO_CONFIG_HPP