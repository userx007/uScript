#include "device_handling.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#define INVALID_INDEX       (-1)

///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES DECLARATION                     //
///////////////////////////////////////////////////////////////////

static int  priv_device_handling_item_exists   ( device_handling_s *psInst, const char *pstrItem );
static bool priv_device_handling_insert_item   ( device_handling_s *psInst, const char *pstrItem );
static bool priv_device_handling_slot_is_empty ( device_handling_s *psInst, const int iIndex );


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////


/**
 * \brief Initialize the list by reseting all the fileds
 * \param[in] psInst pointer to a structure containing the working context
 * \return none
*/

void device_handling_init( device_handling_s *psInst )
{
    memset(psInst->device_handling_list_array, 0, sizeof(psInst->device_handling_list_array));
    memset(psInst->device_handling_list_flags, 0, sizeof(psInst->device_handling_list_flags));
}


/**
 * \brief Function to handle the new status of devices
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrInput new device to be handled to the list
 * \param[in] pstrItem buffer where to return the name of the inserted/removed device
 * \param[in] bOptype type of the operation to be handled, insert/remove
 */

bool device_handling_process( device_handling_s *psInst, const char *pstrInput, char *pstrItem, const bool bOptype )
{
    bool  bItemUpdate = false;

    if( OP_ITEM_INSERT == bOptype )
    {
        // if pstrInput is a new device (not already present in the list) then insert it now and also return it to the caller via pstrItem
        if( true == priv_device_handling_insert_item(psInst, pstrInput) )
        {
#if defined(_MSC_VER)
            strncpy_s(pstrItem, MAX_ITEM_SIZE, pstrInput, strlen(pstrInput));
#else
            strncpy(pstrItem, pstrInput, MAX_ITEM_SIZE);
#endif
            bItemUpdate = true;
        }
    }
    else // if( OP_ITEM_REMOVE == bOptype )
    {
        // get the index of the searched device
        int iIndex = priv_device_handling_item_exists(psInst, pstrInput);

        // if it exists in the list then set it's flag to mark it
        if( INVALID_INDEX != iIndex )
        {
            psInst->device_handling_list_flags[iIndex] = true;
        }
    }

    return bItemUpdate;

}


/**
 * \brief Function to return the removed device
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrItem buffer where to return the name of the removed device
 */

bool device_handling_get_removed ( device_handling_s *psInst, char *pstrItem )
{
    bool bRetVal = false;

    for(int i = 0; i < MAX_LIST_SIZE; ++i)
    {
        if( ('\0' != psInst->device_handling_list_array[i][0]) && (false == psInst->device_handling_list_flags[i]) )
        {
#if defined(_MSC_VER)
            strncpy_s(pstrItem, MAX_ITEM_SIZE, psInst->device_handling_list_array[i], strlen(psInst->device_handling_list_array[i]));
#else
            strncpy(pstrItem, psInst->device_handling_list_array[i], MAX_ITEM_SIZE);
#endif
            psInst->device_handling_list_array[i][0] = '\0';
            bRetVal = true;
            break;
        }
    }

    return bRetVal;

}

/**
 * \brief Function to reset all the flags
 * \param[in] psInst pointer to a structure containing the working context
 */

void device_handling_reset_all_flags ( device_handling_s *psInst )
{
    memset(psInst->device_handling_list_flags, 0, sizeof(psInst->device_handling_list_flags));
}



///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/**
 * \brief Check if an entry (item) is already present in the list
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrItem item to be checked
 * \return index of the item in the list if the item exists or INVALID_INDEX if the item wasn't found
*/

static int priv_device_handling_item_exists ( device_handling_s *psInst, const char *pstrItem )
{
    int iIndex = INVALID_INDEX;

    for( int i = 0; i < MAX_LIST_SIZE; ++i )
    {
        if( 0 == strncmp(psInst->device_handling_list_array[i], pstrItem, MAX_ITEM_SIZE) )
        {
            iIndex = i;
            break;
        }
    }

    return iIndex;

}


/**
 * \brief Check is the slot of the list at given iIndex is available to store a new entry (item)
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] iIndex index in the list of devices
 * \return true if the slot is empty, false otherwise
*/

static bool priv_device_handling_slot_is_empty ( device_handling_s *psInst, const int iIndex )
{
    return (('\0' == psInst->device_handling_list_array[iIndex][0]) ? true : false);

}


/**
 * \brief Insert a new item into the list
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrItem item to be inserted
 * \return true if the slot is empty, false otherwise
*/

static bool priv_device_handling_insert_item ( device_handling_s *psInst, const char *pstrItem )
{
    bool bInserted   = false;

    if( INVALID_INDEX == priv_device_handling_item_exists(psInst, pstrItem) )
    {
        for( int i = 0; i < MAX_LIST_SIZE; ++i )
        {
            if( true == priv_device_handling_slot_is_empty(psInst, i) )
            {
#if defined(_MSC_VER)
                strncpy_s(psInst->device_handling_list_array[i], MAX_ITEM_SIZE, pstrItem, strlen(pstrItem));
#else
                strncpy(psInst->device_handling_list_array[i], pstrItem, MAX_ITEM_SIZE);
#endif
                bInserted = true;
                break;
            }
        }
    }

    return bInserted;

}
