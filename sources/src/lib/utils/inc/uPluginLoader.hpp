#ifndef UPLUGIN_LOADER_H
#define UPLUGIN_LOADER_H

#include <string>
#include <memory>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <system_error>

#ifdef _WIN32
    #include <windows.h>
    using LibHandle = HMODULE;
#else
    #include <dlfcn.h>
    using LibHandle = void*;
#endif

//------------------------------------------------------------------------------
// RAII wrapper for library handle
//------------------------------------------------------------------------------

class LibraryHandle
{
public:
    explicit LibraryHandle(LibHandle handle = nullptr) noexcept
        : handle_(handle)
    {}

    ~LibraryHandle() noexcept
    {
        close();
    }

    // Move semantics
    LibraryHandle(LibraryHandle&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    LibraryHandle& operator=(LibraryHandle&& other) noexcept
    {
        if (this != &other)
        {
            close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Delete copy operations
    LibraryHandle(const LibraryHandle&) = delete;
    LibraryHandle& operator=(const LibraryHandle&) = delete;

    LibHandle get() const noexcept { return handle_; }
    LibHandle release() noexcept
    {
        LibHandle h = handle_;
        handle_ = nullptr;
        return h;
    }

    explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
    void close() noexcept
    {
        if (handle_)
        {
#ifdef _WIN32
            FreeLibrary(handle_);
#else
            dlclose(handle_);
#endif
            handle_ = nullptr;
        }
    }

    LibHandle handle_;
};

//------------------------------------------------------------------------------
// Template alias container for plugin types
//------------------------------------------------------------------------------

template<typename TPluginInterface>
struct PluginTypes {
#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
    using PluginEntry = TPluginInterface* (*)(void* pvUserData);
#else
    using PluginEntry = TPluginInterface* (*)();
#endif
    using PluginExit = void (*)(TPluginInterface*);
    using PluginHandle = std::pair<LibHandle, std::shared_ptr<TPluginInterface>>;
};

//------------------------------------------------------------------------------
// Error information for plugin loading
//------------------------------------------------------------------------------

struct PluginLoadError
{
    enum class ErrorType
    {
        FileNotFound,
        LibraryLoadFailed,
        EntryPointNotFound,
        ExitPointNotFound,
        InitializationFailed
    };

    ErrorType type;
    std::string message;
    std::string pluginName;

    PluginLoadError(ErrorType t, std::string msg, std::string name = "")
        : type(t), message(std::move(msg)), pluginName(std::move(name))
    {}
};

//------------------------------------------------------------------------------
// Result type for plugin loading
//------------------------------------------------------------------------------

template<typename T>
using PluginResult = std::pair<T, std::optional<PluginLoadError>>;

//------------------------------------------------------------------------------
// Utility functor to generate plugin pathname
//------------------------------------------------------------------------------

class PluginPathGenerator
{
public:
    PluginPathGenerator(std::string directory, std::string prefix, std::string extension)
        : pluginDirectory_(ensureTrailingSeparator(std::move(directory)))
        , pluginPrefix_(std::move(prefix))
        , pluginExtension_(ensureLeadingDot(std::move(extension)))
    {}

    std::filesystem::path operator()(const std::string& pluginName) const
    {
        return std::filesystem::path(pluginDirectory_) / (pluginPrefix_ + tolowercase(pluginName) + pluginExtension_);
    }

    // Allow conversion to string for backwards compatibility
    std::string getPathString(const std::string& pluginName) const
    {
        return operator()(pluginName).string();
    }

private:
    std::string pluginDirectory_;
    std::string pluginPrefix_;
    std::string pluginExtension_;

    static std::string tolowercase(const std::string& input)
    {
        std::string result;
        result.reserve(input.size());
        std::transform(input.begin(), input.end(), std::back_inserter(result),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    static std::string ensureTrailingSeparator(std::string path)
    {
        if (!path.empty() && path.back() != '/' && path.back() != '\\')
        {
            path += '/';
        }
        return path;
    }

    static std::string ensureLeadingDot(std::string ext)
    {
        if (!ext.empty() && ext.front() != '.')
        {
            ext.insert(ext.begin(), '.');
        }
        return ext;
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
    operator()(LibHandle handle) const noexcept
    {
        if (!handle)
        {
            return { nullptr, nullptr };
        }

#ifdef _WIN32
        auto entry = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginEntry>(
            GetProcAddress(handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginExit>(
            GetProcAddress(handle, exitName_.c_str()));
#else
        // Clear any previous errors
        dlerror();
        auto entry = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginEntry>(
            dlsym(handle, entryName_.c_str()));
        auto exit = reinterpret_cast<typename PluginTypes<TPluginInterface>::PluginExit>(
            dlsym(handle, exitName_.c_str()));
#endif
        return { entry, exit };
    }

    const std::string& getEntryName() const noexcept { return entryName_; }
    const std::string& getExitName() const noexcept { return exitName_; }

private:
    std::string entryName_;
    std::string exitName_;
};

//------------------------------------------------------------------------------
// Platform-specific library loading utilities
//------------------------------------------------------------------------------

namespace detail {

    inline LibHandle loadLibrary(const std::filesystem::path& path) noexcept
    {
#ifdef _WIN32
        return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
        return dlopen(path.c_str(), RTLD_NOW);
#endif
    }

    inline std::string getLastLoadError()
    {
#ifdef _WIN32
        DWORD error = GetLastError();
        if (error == 0) return "Unknown error";
        
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&messageBuffer, 0, nullptr);
        
        std::string message(messageBuffer, size);
        LocalFree(messageBuffer);
        return message;
#else
        const char* error = dlerror();
        return error ? error : "Unknown error";
#endif
    }

} // namespace detail

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

    PluginResult<PluginHandle> loadWithError(const std::string& pluginName) const
    {
        PluginHandle resultHandle{ nullptr, nullptr };
        std::filesystem::path pluginPath = pathGen_(pluginName);

        // Check if file exists
        if (!std::filesystem::exists(pluginPath))
        {
            return { resultHandle, PluginLoadError{
                PluginLoadError::ErrorType::FileNotFound,
                "Plugin file not found: " + pluginPath.string(),
                pluginName
            }};
        }

        // Load the library
        LibraryHandle libHandle(detail::loadLibrary(pluginPath));
        if (!libHandle)
        {
            return { resultHandle, PluginLoadError{
                PluginLoadError::ErrorType::LibraryLoadFailed,
                "Failed to load library: " + detail::getLastLoadError(),
                pluginName
            }};
        }

        // Resolve entry points
        auto [pluginEntry, pluginExit] = resolver_.template operator()<TPluginInterface>(libHandle.get());

        if (!pluginEntry)
        {
            return { resultHandle, PluginLoadError{
                PluginLoadError::ErrorType::EntryPointNotFound,
                "Entry point '" + resolver_.getEntryName() + "' not found",
                pluginName
            }};
        }

        if (!pluginExit)
        {
            return { resultHandle, PluginLoadError{
                PluginLoadError::ErrorType::ExitPointNotFound,
                "Exit point '" + resolver_.getExitName() + "' not found",
                pluginName
            }};
        }

        // Initialize the plugin
#if (1 == USE_PLUGIN_ENTRY_WITH_USERDATA)
        void* userData = nullptr; // Replace with actual user data if needed
        TPluginInterface* rawPlugin = pluginEntry(userData);
#else
        TPluginInterface* rawPlugin = pluginEntry();
#endif

        if (!rawPlugin)
        {
            return { resultHandle, PluginLoadError{
                PluginLoadError::ErrorType::InitializationFailed,
                "Plugin initialization returned null",
                pluginName
            }};
        }

        // Create a custom deleter that properly manages the library handle lifetime
        // We need to keep the library loaded as long as the plugin interface is alive
        LibHandle rawHandle = libHandle.release();
        
        std::shared_ptr<TPluginInterface> shpPlugin(
            rawPlugin,
            [rawHandle, pluginExit](TPluginInterface* p) {
                if (p)
                {
                    pluginExit(p);
                }
                // Clean up the library handle after the plugin is destroyed
#ifdef _WIN32
                FreeLibrary(rawHandle);
#else
                dlclose(rawHandle);
#endif
            });

        resultHandle = { rawHandle, shpPlugin };
        return { resultHandle, std::nullopt };
    }

    // Original interface (backwards compatible, returns empty handle on error)
    PluginHandle operator()(const std::string& pluginName) const
    {
        auto [handle, error] = loadWithError(pluginName);
        return handle;
    }

private:
    PathGenerator pathGen_;
    EntryPointResolver resolver_;
};

//------------------------------------------------------------------------------
// Convenience factory functions
//------------------------------------------------------------------------------

namespace plugin_loader {

    /**
     * \brief Create a plugin loader with default path generation
     * \param directory Base directory for plugins
     * \param prefix Prefix for plugin files (e.g., "lib")
     * \param extension File extension (e.g., ".so" or ".dll")
     * \param entryPoint Name of the entry function
     * \param exitPoint Name of the exit function
     */
    template<typename TPluginInterface>
    auto makeLoader(
        const std::string& directory,
        const std::string& prefix,
        const std::string& extension,
        const std::string& entryPoint,
        const std::string& exitPoint)
    {
        return PluginLoaderFunctor<TPluginInterface>(
            PluginPathGenerator(directory, prefix, extension),
            PluginEntryPointResolver(entryPoint, exitPoint)
        );
    }

} // namespace plugin_loader

#endif /* UPLUGIN_LOADER_H */
