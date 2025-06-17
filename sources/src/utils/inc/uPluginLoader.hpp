#ifndef UPLUGIN_LOADER_H
#define UPLUGIN_LOADER_H

#include <string>
#include <memory>
#include <utility>
#include <algorithm>
#include <filesystem>

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

template<typename TPluginInterface>
struct PluginTypes {
#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
    using PluginEntry = TPluginInterface * (*)(void* pvUserData);
#else
    using PluginEntry = TPluginInterface * (*)();
#endif
    using PluginExit = void (*)(TPluginInterface*);
    using PluginHandle = std::pair<LibHandle, std::shared_ptr<TPluginInterface>>;
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

    template<typename TPluginInterface>
    std::pair<typename PluginTypes<TPluginInterface>::PluginEntry,
        typename PluginTypes<TPluginInterface>::PluginExit>
        operator()(LibHandle handle) const
    {
#ifdef _WIN32
        auto entry = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginEntry>(
                         GetProcAddress((HMODULE)handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginExit>(
                        GetProcAddress((HMODULE)handle, exitName_.c_str()));
#else
        auto entry = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginEntry>(
                         dlsym(handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginExit>(
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
    typename TPluginInterface,
    typename PathGenerator = PluginPathGenerator,
    typename EntryPointResolver = PluginEntryPointResolver
    >
class PluginLoaderFunctor
{
public:
    using PluginEntry = typename PluginTypes<TPluginInterface>::PluginEntry;
    using PluginExit = typename PluginTypes<TPluginInterface>::PluginExit;
    using PluginHandle = typename PluginTypes<TPluginInterface>::PluginHandle;

    PluginLoaderFunctor(PathGenerator pathGen, EntryPointResolver resolver)
        : pathGen_(std::move(pathGen))
        , resolver_(std::move(resolver))
        {}

    PluginHandle operator()(const std::string& pluginName) const
    {
        PluginHandle aRetVal{ nullptr, nullptr };
        std::string strPluginPathName = pathGen_(pluginName);

        if (std::filesystem::exists(strPluginPathName)) {
#ifdef _WIN32
            LibHandle hPlugin = LoadLibraryEx(TEXT(strPluginPathName.c_str()), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
            LibHandle hPlugin = dlopen(strPluginPathName.c_str(), RTLD_NOW);
#endif

            if (!hPlugin) {
                return aRetVal;
            }

            auto [pluginEntry, pluginExit] = resolver_.template operator()<TPluginInterface>(hPlugin);

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
            std::shared_ptr<TPluginInterface> shpEntryPoint(pluginEntry(userData), pluginExit);
#else
            std::shared_ptr<TPluginInterface> shpEntryPoint(pluginEntry(), pluginExit);
#endif
            return { hPlugin, shpEntryPoint };
        } else {
            return aRetVal;
        }
    }

private:
    PathGenerator pathGen_;
    EntryPointResolver resolver_;
};

#endif /* UPLUGIN_LOADER_H */