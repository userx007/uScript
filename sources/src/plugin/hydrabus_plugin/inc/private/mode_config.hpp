#pragma once

///////////////////////////////////////////////////////////////////
//  MODE_CMD_RECORD(tag, mode_byte, repetitions, banner_string)  //
//  - tag           : enum tag + string key                      //
//  - mode_byte     : byte sent to enter the mode                //
//  - repetitions   : how many times to send mode_byte           //
//  - banner_string : 4-char response expected from firmware     //
///////////////////////////////////////////////////////////////////

#define MODE_COMMANDS_CONFIG_TABLE              \
MODE_CMD_RECORD( bbio,      0x00, 20, BBIO1 )  \
MODE_CMD_RECORD( spi,       0x01,  1, SPI1  )  \
MODE_CMD_RECORD( i2c,       0x02,  1, I2C1  )  \
MODE_CMD_RECORD( uart,      0x03,  1, ART1  )  \
MODE_CMD_RECORD( onewire,   0x04,  1, 1W01  )  \
MODE_CMD_RECORD( rawwire,   0x05,  1, RAW1  )  \
MODE_CMD_RECORD( smartcard, 0x0B,  1, CRD1  )  \
MODE_CMD_RECORD( nfc,       0x0C,  1, NFC1  )  \
MODE_CMD_RECORD( mmc,       0x0D,  1, MMC1  )  \
MODE_CMD_RECORD( sdio,      0x0E,  1, SDI1  )  \
MODE_CMD_RECORD( swd,       0x05,  1, RAW1  )
