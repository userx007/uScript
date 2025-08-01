#ifndef _DEVICE_HANDLING__H_
#define _DEVICE_HANDLING__H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_ITEM_SIZE          (32U)
#define MAX_LIST_SIZE          (32U)

/* operations */
#define OP_ITEM_INSERT         true
#define OP_ITEM_REMOVE         false

#define CTX_INITIAL_DETECTION  true
#define CTX_RUNTIME_DETECTION  false


typedef struct device_handling_s_
{
    char device_handling_list_array[MAX_LIST_SIZE][MAX_ITEM_SIZE];
    bool device_handling_list_flags[MAX_LIST_SIZE];

} device_handling_s;

/**
 * \brief Initialize the list by reseting all the fileds
 * \param[in] psInst pointer to a structure containing the working context
 * \return none
*/

void device_handling_init ( device_handling_s *psInst );


/**
 * \brief Function to handle the new status of devices
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrInput new device to be handled to the list
 * \param[in] pstrItem buffer where to return the name of the inserted/removed device
 * \param[in] bOptype type of the operation to be handled, insert/remove
 */

bool device_handling_process ( device_handling_s *psInst, const char *pstrInput, char *pstrItem, const bool bOpType );


/**
 * \brief Function to return the removed device
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrItem buffer where to return the name of the removed device
 */

bool device_handling_get_removed ( device_handling_s *psInst, char *pstrItem );


/**
 * \brief Function to reset all the flags
 * \param[in] psInst pointer to a structure containing the working context
 */

void device_handling_reset_all_flags ( device_handling_s *psInst );


#ifdef __cplusplus
}
#endif

#endif //_DEVICE_HANDLING__H_
