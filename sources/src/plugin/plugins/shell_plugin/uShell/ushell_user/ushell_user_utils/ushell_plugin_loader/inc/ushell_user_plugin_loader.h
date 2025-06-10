#ifndef UPLUGIN_LOADER_H
#define UPLUGIN_LOADER_H

#include <string>
#include <memory>
#include <utility>
#include <algorithm>

#define PLUGIN_PATH          "plugins/"
#define PLUGIN_PREFIX        "lib"
#ifdef _WIN32
    #include <windows.h>
    #define PLUGIN_EXTENSION "_plugin.dll"
    using LibHandle = HMODULE;
#else
    #include <dlfcn.h>
    #define PLUGIN_EXTENSION "_plugin.so"
    using LibHandle = void*;
#endif



//------------------------------------------------------------------------------
// Template alias container for plugin types
//------------------------------------------------------------------------------

template<typename uShellPluginInterface>
struct PluginTypes {
#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
    using uShellPluginEntry = uShellPluginInterface * (*)(void* pvUserData);
#else
    using uShellPluginEntry = uShellPluginInterface * (*)();
#endif
    using uShellPluginExit = void (*)(uShellPluginInterface*);
    using PluginHandle = std::pair<LibHandle, std::shared_ptr<uShellPluginInterface>>;
};



//------------------------------------------------------------------------------
// Utility functor to generate plugin pathname
//------------------------------------------------------------------------------

class PluginPathGenerator
{
public:
    PluginPathGenerator(std::string directory = PLUGIN_PATH, std::string prefix = PLUGIN_PREFIX, std::string extension = PLUGIN_EXTENSION)
        : pluginDirectory_(std::move(directory)),
          pluginPrefix_(std::move(prefix)),
          pluginExtension_(std::move(extension)) {}

    std::string operator()(const std::string& pluginName) const
    {
        return pluginDirectory_ + pluginPrefix_ + tolowercase(pluginName) + pluginExtension_;
    }

private:
    std::string pluginDirectory_;
    std::string pluginPrefix_;
    std::string pluginExtension_;

    static std::string tolowercase(const std::string& input)
    {
        std::string result = input;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        return result;
    }
};



//------------------------------------------------------------------------------
// Functor to resolve entry points
//------------------------------------------------------------------------------

class DefaultEntryPointResolver
{
public:
    DefaultEntryPointResolver(std::string entryName = "uShellPluginEntry", std::string exitName = "uShellPluginExit")
        : entryName_(std::move(entryName)), exitName_(std::move(exitName)) {}

    template<typename uShellPluginInterface>
    std::pair<typename PluginTypes<uShellPluginInterface>::uShellPluginEntry,
        typename PluginTypes<uShellPluginInterface>::uShellPluginExit>
        operator()(LibHandle handle) const
    {
#ifdef _WIN32
        auto entry = reinterpret_cast<typename PluginTypes<uShellPluginInterface>::uShellPluginEntry>(
                         GetProcAddress((HMODULE)handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<uShellPluginInterface>::uShellPluginExit>(
                        GetProcAddress((HMODULE)handle, exitName_.c_str()));
#else
        auto entry = reinterpret_cast<typename PluginTypes<uShellPluginInterface>::uShellPluginEntry>(
                         dlsym(handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<uShellPluginInterface>::uShellPluginExit>(
                        dlsym(handle, exitName_.c_str()));
#endif
        return { entry, exit };
    }

private:
    std::string entryName_;
    std::string exitName_;
};



//------------------------------------------------------------------------------
// Template-based functor to load plugin
//------------------------------------------------------------------------------

template <
    typename uShellPluginInterface,
    typename PathGenerator = PluginPathGenerator,
    typename EntryPointResolver = DefaultEntryPointResolver
    >
class PluginLoaderFunctor
{
public:
    using uShellPluginEntry = typename PluginTypes<uShellPluginInterface>::uShellPluginEntry;
    using uShellPluginExit = typename PluginTypes<uShellPluginInterface>::uShellPluginExit;
    using PluginHandle = typename PluginTypes<uShellPluginInterface>::PluginHandle;

    PluginLoaderFunctor(PathGenerator pathGen = PathGenerator(),
                        EntryPointResolver resolver = EntryPointResolver())
        : pathGen_(std::move(pathGen)), resolver_(std::move(resolver)) {}

    PluginHandle operator()(const std::string& pluginName) const
    {
        PluginHandle aRetVal{ nullptr, nullptr };

        std::string strPluginPathName = pathGen_(pluginName);

#ifdef _WIN32
        LibHandle hPlugin = LoadLibraryEx(TEXT(strPluginPathName.c_str()), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
        LibHandle hPlugin = dlopen(strPluginPathName.c_str(), RTLD_NOW);
#endif

        if (!hPlugin) {
            return aRetVal;
        }

        auto [uShellPluginEntry, uShellPluginExit] = resolver_.template operator()<uShellPluginInterface>(hPlugin);

        if (!uShellPluginEntry || !uShellPluginExit) {
#ifdef _WIN32
            FreeLibrary(hPlugin);
#else
            dlclose(hPlugin);
#endif
            return aRetVal;
        }

#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
        void* userData = nullptr; // Replace with actual user data if needed
        std::shared_ptr<uShellPluginInterface> shpEntryPoint(uShellPluginEntry(userData), uShellPluginExit);
#else
        std::shared_ptr<uShellPluginInterface> shpEntryPoint(uShellPluginEntry(), uShellPluginExit);
#endif
        return { hPlugin, shpEntryPoint };
    }

private:
    PathGenerator pathGen_;
    EntryPointResolver resolver_;
};

#endif /* UPLUGIN_LOADER_H */