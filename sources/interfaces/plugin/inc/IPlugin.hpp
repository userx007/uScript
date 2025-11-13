
#ifndef IPLUGIN_HPP
#define IPLUGIN_HPP

#include <memory>
#include <string>


///////////////////////////////////////////////////////////////////
//                 EXTERN DATA DECLARATIONS                      //
///////////////////////////////////////////////////////////////////

struct PluginDataSet;
struct PluginDataGet;


///////////////////////////////////////////////////////////////////
//                     ABSTRACT PLUGIN INTERFACE                 //
///////////////////////////////////////////////////////////////////


class PluginInterface
{
public:

    /**< class destructor */
    virtual ~PluginInterface() = default;

    /** < interface used to initialize the plugin */
    virtual bool do_init ( void *pvUserData ) = 0;

    /** < interface used to enable the real command execution */
    virtual void do_enable ( void ) = 0;

    /** < interface used to dispatch commands */
    virtual bool do_dispatch ( const std::string& strCmd, const std::string& strParams ) const = 0;

    /** < interface used to de-initialize the plugin */
    virtual void do_cleanup ( void ) = 0;

    /** < interface used to set parameters to plugin */
    virtual bool set_params ( const PluginDataSet *psParams ) = 0;

    /** < interface used to get parameters from plugin */
    virtual void get_params ( PluginDataGet *psParams ) const = 0;

    /** < interface used to get the result data */
    virtual const std::string& get_data ( void ) const = 0;

    /** < interface used to reset the result data */
    virtual void reset_data( void ) const = 0;

    /** < interface used to return the initialization state of the plugin */
    virtual bool is_initialized ( void ) const = 0;

    /** < interface used to return the enabling state of the plugin */
    virtual bool is_enabled ( void ) const = 0;

    /** < interface used to get the privileged status (if can access the caller's structures) */
    virtual bool is_privileged ( void ) const = 0;

    /** < interface used to set the fault tolerant mode */
    virtual bool is_fault_tolerant ( void ) const = 0;
};


/**
 * \brief definition of the function pointer returning the plugin entry/exit pointers
*/
using PluginEntry = PluginInterface * (*)();
using PluginExit  = void (*)(PluginInterface*);

#endif /* IPLUGIN_HPP */