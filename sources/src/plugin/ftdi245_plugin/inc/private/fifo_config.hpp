#ifndef FT245_FIFO_CONFIG_HPP
#define FT245_FIFO_CONFIG_HPP

///////////////////////////////////////////////////////////////////
//              FIFO subcommand configurator                     //
//                                                               //
//  FT245 FIFO is a simple byte-stream interface.                //
//  Both async (FT245BM + FT245R) and sync (FT245BM only)        //
//  FIFO modes are supported.                                    //
//                                                               //
//  Note: no clock divisor; the USB bulk transfer rate is        //
//  governed by the USB frame clock and host bandwidth.          //
///////////////////////////////////////////////////////////////////

#define FIFO_COMMANDS_CONFIG_TABLE  \
FIFO_CMD_RECORD( open   )           \
FIFO_CMD_RECORD( close  )           \
FIFO_CMD_RECORD( cfg    )           \
FIFO_CMD_RECORD( write  )           \
FIFO_CMD_RECORD( read   )           \
FIFO_CMD_RECORD( wrrd   )           \
FIFO_CMD_RECORD( wrrdf  )           \
FIFO_CMD_RECORD( flush  )           \
FIFO_CMD_RECORD( script )           \
FIFO_CMD_RECORD( help   )

#endif // FT245_FIFO_CONFIG_HPP