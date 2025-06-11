#ifndef UPLUGIN_LOADER_H
#define UPLUGIN_LOADER_H

#include <string>
#include <memory>
#include <utility>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    using LibHandle = HMODULE;
#else
    #include <dlfcn.h>
    using LibHandle = void*;
#endif



//------------------------------------------------------------------------------
// Template alias container for plugin types
//------------------------------------------------------------------------------

template<typename PluginInterface>
struct PluginTypes {
#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
    using PluginEntry = PluginInterface * (*)(void* pvUserData);
#else
    using PluginEntry = PluginInterface * (*)();
#endif
    using PluginExit = void (*)(PluginInterface*);
    using PluginHandle = std::pair<LibHandle, std::shared_ptr<PluginInterface>>;
};



//------------------------------------------------------------------------------
// Utility functor to generate plugin pathname
//------------------------------------------------------------------------------

class PluginPathGenerator
{
public:
    PluginPathGenerator(std::string directory, std::string prefix, std::string extension)
        : pluginDirectory_(std::move(directory))
        , pluginPrefix_(std::move(prefix))
        , pluginExtension_(std::move(extension))
        {}

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

class PluginEntryPointResolver
{
public:
    PluginEntryPointResolver(std::string entryName, std::string exitName)
        : entryName_(std::move(entryName))
        , exitName_(std::move(exitName))
        {}

    template<typename PluginInterface>
    std::pair<typename PluginTypes<PluginInterface>::PluginEntry,
        typename PluginTypes<PluginInterface>::PluginExit>
        operator()(LibHandle handle) const
    {
#ifdef _WIN32
        auto entry = reinterpret_cast<typename PluginTypes<PluginInterface>::PluginEntry>(
                         GetProcAddress((HMODULE)handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<PluginInterface>::PluginExit>(
                        GetProcAddress((HMODULE)handle, exitName_.c_str()));
#else
        auto entry = reinterpret_cast<typename PluginTypes<PluginInterface>::PluginEntry>(
                         dlsym(handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<PluginInterface>::PluginExit>(
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
    typename PluginInterface,
    typename PathGenerator = PluginPathGenerator,
    typename EntryPointResolver = PluginEntryPointResolver
    >
class PluginLoaderFunctor
{
public:
    using PluginEntry = typename PluginTypes<PluginInterface>::PluginEntry;
    using PluginExit = typename PluginTypes<PluginInterface>::PluginExit;
    using PluginHandle = typename PluginTypes<PluginInterface>::PluginHandle;

    PluginLoaderFunctor(PathGenerator pathGen, EntryPointResolver resolver)
        : pathGen_(std::move(pathGen))
        , resolver_(std::move(resolver))
        {}

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

        auto [pluginEntry, pluginExit] = resolver_.template operator()<PluginInterface>(hPlugin);

        if (!pluginEntry || !pluginExit) {
#ifdef _WIN32
            FreeLibrary(hPlugin);
#else
            dlclose(hPlugin);
#endif
            return aRetVal;
        }

#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
        void* userData = nullptr; // Replace with actual user data if needed
        std::shared_ptr<PluginInterface> shpEntryPoint(pluginEntry(userData), pluginExit);
#else
        std::shared_ptr<PluginInterface> shpEntryPoint(pluginEntry(), pluginExit);
#endif
        return { hPlugin, shpEntryPoint };
    }

private:
    PathGenerator pathGen_;
    EntryPointResolver resolver_;
};

#endif /* UPLUGIN_LOADER_H */