/**
 * \file    uart_port_handling.hpp
 * \brief   Interface header of UART port handling detection
 *
 * \par Responsibility
 * - SW-Subsystem:         System Software - SSW
 * - SW-Domain:            Software Loading
 * - Package ID:           None
 *
 * \par Change history
 * \verbatim
 *  Date          Author                Reason
 *  10.01.2022    Victor Marian Popa    IIP-118030   Initial implementation
 * \endverbatim
 *
 * \par Copyright Notice:
 * \verbatim
 * SPDX-FileCopyrightText: Copyright (C) 2021 Continental AG and subsidiaries
 * SPDX-License-Identifier: LicenseRef-Continental-1.0
 * \endverbatim
 */

#ifndef __UART_PORT_HANDLING__
#define __UART_PORT_HANDLING__

#include <stdint.h>
#include <stdbool.h>
#include <atomic>


///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////


/**
 * definition of pointer to function used for UART handling
 */

typedef bool ( *PFUARTHDL )( char*, const uint32_t, const uint32_t, const uint32_t );


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES DECLARATION                      //
///////////////////////////////////////////////////////////////////

/**
 * \brief Function to wait for UART port insertion
 * \param[in] pstrItem buffer where to return the name of the inserted port (COMn)
 * \param[in] uiItemSize size of the buffer  where to return the name of the inserted port
 * \param[in] uiTimeout timeout to wait for insertion (if 0 then wait forewer)
 * \param[in] uiPollingInterval poling interval to wait for USB insertion
 * \return true on changes, false otherwise
 */

bool uart_wait_port_insert( char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval );


/**
 * \brief Function to wait for UART port removal
 * \param[in] pstrItem buffer where to return the name of the removed port (COMn)
 * \param[in] uiItemSize size of the buffer  where to return the name of the removed port
 * \param[in] uiTimeout timeout to wait for removal (if 0 then wait forewer)
 * \param[in] uiPollingInterval poling interval to wait for USB removal
 * \return true on changes, false otherwise
 */

bool uart_wait_port_remove( char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval );


/**
 * \brief Function to monitorize the UART port insertion and removal
 * \param[in] uiPollingInterval poling interval to wait for port insertion
 * \param[in] bRun flag used to end the monitoring loop
 * \return true on changes, false otherwise
 */

bool uart_monitor( const uint32_t uiPollingInterval, std::atomic<bool> &bRun );


/**
 * \brief Get the number of UART ports found in the system at the moment of the call
 * \param none
 * \return number of UART ports found at the moment of the call
 */

uint32_t uart_get_available_ports_number( void );

/**
 * \brief Print the list of UART ports
 * \param none
 * \return void
 */

void uart_list_ports( const char *pstrCaption = nullptr );


#endif /* __UART_PORT_HANDLING__ */
