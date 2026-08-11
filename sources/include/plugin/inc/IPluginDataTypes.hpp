#ifndef I_PLUGIN_DATA_TYPES_HPP
#define I_PLUGIN_DATA_TYPES_HPP


#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#ifdef _WIN32
    #include <windows.h>
#endif

// forward declaration
class  PluginInterface;
struct LogBuffer;

// definition of pointer to plugin interface
using PluginInterfacePtr = std::shared_ptr<PluginInterface>;

#ifdef _WIN32
    using LibHandle = HMODULE;
#else
    using LibHandle = void*;
#endif

// information to be set to a plugin
struct PluginDataSet {
    std::shared_ptr<LogBuffer>  shpLogger;
    std::unordered_map<std::string, std::string> mapSettings;

    // Runtime identity of this plugin instance, as known to the script
    // interpreter/GUI -- e.g. "UART" for the base instance, "UART:1" for an
    // auto-instantiated second instance (see ScriptInterpreter::
    // m_autoInstantiatePlugins() and PluginPathGenerator's ":N" suffix
    // handling in uPluginLoader.hpp). Plugins should use this (falling back
    // to their own fixed *_PLUGIN_NAME constant when empty, e.g. when
    // constructed directly/standalone rather than through the interpreter)
    // as the plugin identity string reported to gui_notify_comm_dump(), so
    // the GUI comm-dump panel can distinguish "UART:1" traffic from "UART:2"
    // instead of both simply showing "UART".
    std::string strInstanceName;
};


// information to be extracted from a plugin
struct PluginDataGet {
    std::string                         strPluginVersion;
    std::vector<std::string>            vstrPluginCommands;
    std::unordered_map<std::string,bool> mapBlockingCommands;  // cmd name → true if endless-loop capable
};


// script plugin definition
struct PluginDataType {
    std::string                 strPluginName;
    std::string                 strPluginVersRule;
    std::string                 strPluginVersRequested;
    PluginInterfacePtr          shptrPluginEntryPoint;
    LibHandle                   hLibHandle;
    PluginDataGet               sGetParams;
    PluginDataSet               sSetParams;
};


#endif /* I_PLUGIN_DATA_TYPES_HPP */