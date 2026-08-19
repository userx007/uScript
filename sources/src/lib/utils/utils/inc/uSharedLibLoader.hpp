#ifndef USHAREDLIBLOADER_HPP
#define USHAREDLIBLOADER_HPP

#include "uError.hpp"

#include <system_error>
#include <type_traits>
#include <string>
#include <utility>

#ifdef _WIN32
    #include <windows.h>
    #include <shlwapi.h>
#else // __linux__
    #include <dlfcn.h>
#endif /* _WIN32 */


///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

/**
 * definitions needed to create a common interface for both, Windows and Linux
 */
#ifndef _WIN32
using FARPROC = void*;
using HMODULE = void*;
using LPCSTR  = const char*;
using LPCTSTR = const char*;
#endif //_WIN32

////////////////////////////////////////////////////////////////////////////////////////////
// Class used to get the address of a process
////////////////////////////////////////////////////////////////////////////////////////////

class ProcAddress
{
public:
    /**
     * \brief class constructor
     */
    explicit constexpr ProcAddress(FARPROC ptr) noexcept : m_procPtr(ptr) {}

    /**
     * \brief overloader operator()
     * \note Allows implicit conversion to function pointer types
     */
    template <typename T>
    operator T *() const noexcept
    {
#if !defined(_MSC_VER)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
        return reinterpret_cast<T *>(m_procPtr);
#if !defined(_MSC_VER)
    #pragma GCC diagnostic pop
#endif
    }

    /**
     * \brief Check if the procedure address is valid
     */
    explicit operator bool() const noexcept
    {
        return m_procPtr != nullptr;
    }

private:
    FARPROC m_procPtr;
};


////////////////////////////////////////////////////////////////////////////////////////////
//  Class used to handle a shared library (load, unload and get the symbols of the library)
////////////////////////////////////////////////////////////////////////////////////////////

class SharedLibLoader
{
public:
    /**
     * \brief Default constructor — creates an unloaded loader.
     *        Call load() to load a library; check isLoaded() or the return value of load().
     */
    SharedLibLoader() noexcept : m_hModule(nullptr) {}

    /**
     * \brief Constructor that attempts to load a shared library.
     * \note On failure the object is left in the unloaded state (isLoaded() == false).
     *       No exception is thrown; call isLoaded() to verify success.
     */
    explicit SharedLibLoader(LPCTSTR pstrFilename) : m_hModule(nullptr)
    {
        load(pstrFilename);
    }

    /**
     * \brief Load (or reload) a shared library.
     * \param pstrFilename Path to the shared library.
     * \return true on success, false if the library could not be loaded.
     */
    bool load(LPCTSTR pstrFilename) noexcept
    {
        unload();
#ifdef _WIN32
        m_hModule = LoadLibraryEx(pstrFilename, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
        m_hModule = dlopen(pstrFilename, RTLD_NOW);
#endif
        return (m_hModule != nullptr);
    }

    /**
     * \brief Move constructor
     */
    SharedLibLoader(SharedLibLoader&& other) noexcept 
        : m_hModule(other.m_hModule)
    {
        other.m_hModule = nullptr;
    }

    /**
     * \brief Move assignment operator
     */
    SharedLibLoader& operator=(SharedLibLoader&& other) noexcept
    {
        if (this != &other)
        {
            // Release current resource
            unload();
            
            // Transfer ownership
            m_hModule = other.m_hModule;
            other.m_hModule = nullptr;
        }
        return *this;
    }

    // Delete copy operations (non-copyable resource)
    SharedLibLoader(const SharedLibLoader&) = delete;
    SharedLibLoader& operator=(const SharedLibLoader&) = delete;

    /**
     * \brief The class destructor
     * \note Unload the shared library
     */
    ~SharedLibLoader() noexcept
    {
        unload();
    }

    /**
     * \brief Get a symbol address, returning false on failure (non-throwing).
     * \param pstrProcName  Name of the exported symbol.
     * \param outAddr       Receives the ProcAddress on success; untouched on failure.
     * \return true if the symbol was found, false otherwise.
     */
    bool getSymbol(LPCSTR pstrProcName, ProcAddress& outAddr) const noexcept
    {
#ifndef _WIN32
        dlerror();
#endif
        FARPROC procPtr =
#ifdef _WIN32
            GetProcAddress(m_hModule, pstrProcName);
#else
            dlsym(m_hModule, pstrProcName);
#endif
        if (procPtr == nullptr) {
            return false;
        }
        outAddr = ProcAddress(procPtr);
        return true;
    }

    /**
     * \brief overload operator [] — kept for backwards compatibility.
     * \note Prefer getSymbol() for error-checked, non-throwing symbol lookup.
     *       Returns an invalid (null) ProcAddress if the symbol is not found.
     */
    ProcAddress operator[](LPCSTR pstrProcName) const noexcept
    {
        ProcAddress addr(nullptr);
        getSymbol(pstrProcName, addr);
        return addr;
    }

    /**
     * \brief Get symbol address without throwing exception
     * \return ProcAddress with nullptr if symbol not found
     */
    ProcAddress tryGetSymbol(LPCSTR pstrProcName) const noexcept
    {
#ifndef _WIN32
        dlerror(); // Clear previous errors
#endif
        FARPROC procPtr =
#ifdef _WIN32
            GetProcAddress(m_hModule, pstrProcName);
#else
            dlsym(m_hModule, pstrProcName);
#endif
        return ProcAddress(procPtr);
    }

    /**
     * \brief Check if the library is loaded
     */
    bool isLoaded() const noexcept
    {
        return m_hModule != nullptr;
    }

    /**
     * \brief Get the native handle (use with caution)
     */
    HMODULE handle() const noexcept
    {
        return m_hModule;
    }

private:
    /**
     * \brief Internal helper to unload the library
     */
    void unload() noexcept
    {
        if (m_hModule != nullptr)
        {
#ifdef _WIN32
            FreeLibrary(m_hModule);
#else
            dlclose(m_hModule);
#endif
            m_hModule = nullptr;
        }
    }

    HMODULE m_hModule;
};

#endif // USHAREDLIBLOADER_HPP
