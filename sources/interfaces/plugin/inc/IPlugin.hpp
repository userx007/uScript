
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

    /** < interface used to return the initialization state of the plugin */
    virtual bool isInitialized ( void ) const = 0;

    /** < interface used to return the enabling state of the plugin */
    virtual bool isEnabled ( void ) const = 0;

    /** < interface used to set parameters to plugin */
    virtual void setParams ( const PluginDataSet *psParams ) = 0;

    /** < interface used to get parameters from plugin */
    virtual void getParams ( PluginDataGet *psParams ) const = 0;

    /** < interface used to get the result data */
    virtual const std::string& getData ( void ) const = 0;

    /** < interface used to reset the result data */
    virtual void resetData( void ) const = 0;

    /** < interface used to initialize the plugin */
    virtual bool doInit ( void *pvUserData ) = 0;

    /** < interface used to enable the real command execution */
    virtual void doEnable ( void ) = 0;

    /** < interface used to dispatch commands */
    virtual bool doDispatch ( const std::string& strCmd, const std::string& strParams ) const = 0;

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

#endif /* IPLUGIN_HPP */