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
     * \brief The class constructor
     * \note Tries to load a shared library and throw an exception if the library cannot be loaded.
     *       The caller has to catch and handle the exception
     */
    explicit SharedLibLoader(LPCTSTR pstrFilename) :
#ifdef _WIN32
        m_hModule(LoadLibraryEx(pstrFilename, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
#else
        m_hModule(dlopen(pstrFilename, RTLD_NOW))
#endif
    {
        if (m_hModule == nullptr)
        {
#ifdef _WIN32
            throw std::system_error(EDOM, std::generic_category(), 
                std::string("Failed to load shared library: ") + pstrFilename + uerror::getLastError());
#else
            const char* error = dlerror();
            throw std::system_error(EDOM, std::generic_category(), 
                std::string("Failed to load shared library: ") + pstrFilename + 
                (error ? std::string(". Error: ") + error : ""));
#endif
        }
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
     * \brief overload operator []
     * \note Returns the address of the symbol specified as string parameter
     */
    ProcAddress operator[](LPCSTR pstrProcName) const
    {
#ifndef _WIN32
        // Clear any previous error conditions
        dlerror();
#endif
        FARPROC procPtr =
#ifdef _WIN32
            GetProcAddress(m_hModule, pstrProcName);
#else
            dlsym(m_hModule, pstrProcName);
#endif

        if (procPtr == nullptr)
        {
#ifdef _WIN32
            throw std::system_error(EDOM, std::generic_category(), 
                std::string("Failed to load symbol: ") + pstrProcName + uerror::getLastError());
#else
            const char* pstrError = dlerror();
            throw std::system_error(EDOM, std::generic_category(), 
                std::string("Failed to load symbol: ") + pstrProcName + 
                std::string(". Error: ") + (pstrError ? pstrError : "unknown"));
#endif
        }

        return ProcAddress(procPtr);
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
