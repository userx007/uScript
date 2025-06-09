#ifndef IPLUGINDATATYPES_HPP
#define IPLUGINDATATYPES_HPP


#include <string>
#include <vector>
#include <map>
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
    std::map<std::string, std::string> mapSettings;

};


// information to be extracted from a plugin
struct PluginDataGet {
    std::string                 strPluginVersion;
    std::vector<std::string>    vstrPluginCommands;

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


#endif /* IPLUGINDATATYPES_HPP */