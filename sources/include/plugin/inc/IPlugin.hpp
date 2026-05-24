
#ifndef I_PLUGIN_HPP
#define I_PLUGIN_HPP

#include <memory>
#include <string>
#include <stop_token>


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

    /** < interface used to return the initialization state of the plugin */
    virtual bool isInitialized ( void ) const = 0;

    /** < interface used to return the enabling state of the plugin */
    virtual bool isEnabled ( void ) const = 0;

    /** < interface used to set parameters to plugin */
    virtual bool setParams ( const PluginDataSet *psParams ) = 0;

    /** < interface used to get parameters from plugin */
    virtual void getParams ( PluginDataGet *psParams ) const = 0;

    /** < interface used to get the result data */
    virtual const std::string& getData ( void ) const = 0;

    /** < interface used to reset the result data */
    virtual void resetData( void ) const = 0;

    /** < interface used to initialize the plugin */
    virtual bool doInit ( void *pvUserData ) = 0;

    /** < interface used to enable the real command execution */
    virtual bool doEnable ( void ) = 0;

    /** < interface used to dispatch commands.
     *
     *  The optional stop_token st is provided when the command was launched
     *  as a threaded command (trailing '&' syntax).  Plugins that may run
     *  indefinitely (e.g. polling loops) MUST periodically check
     *  st.stop_requested() and return when it becomes true.
     *
     *  When called sequentially (no '&') st is a default-constructed token
     *  whose stop_requested() always returns false, so existing plugin
     *  implementations that ignore the parameter behave identically to before.
     */
    virtual bool doDispatch ( const std::string& strCmd,
                              const std::string& strParams,
                              std::stop_token    st = {} ) const = 0;

    /** < interface used to de-initialize the plugin */
    virtual void doCleanup ( void ) = 0;

    /** < interface used to set the fault tolerant mode */
    virtual bool isFaultTolerant ( void ) const = 0;

    /** < interface used to get the privileged status (if can access the caller's structures) */
    virtual bool isPrivileged ( void ) const = 0;

};


/**
 * \brief definition of the function pointer returning the plugin entry/exit pointers
*/
using PluginEntry = PluginInterface * (*)();
using PluginExit  = void (*)(PluginInterface*);

#endif /* I_PLUGIN_HPP */