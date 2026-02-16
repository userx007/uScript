#ifndef USHAREDLIBLOADER_HPP
#define USHAREDLIBLOADER_HPP

#include "uError.hpp"

#include <system_error>
#include <type_traits>
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

    /*
     * \brief class constructor
     */

    explicit ProcAddress(FARPROC ptr) : m_procPtr(ptr) {}


    /*
     * \brief overloader operator()
     */

    template <typename T>
    operator T *() const
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


private:

    FARPROC m_procPtr;

};


////////////////////////////////////////////////////////////////////////////////////////////
//  Class used to handle a shared library (load, unload and get the symbols of the library)
////////////////////////////////////////////////////////////////////////////////////////////

class SharedLibLoader
{

public:

    /*
     * \brief The class constructor
     * \note Tries to load a shared library and throw an exception if the library cannot be loaded.
     *       The caller has to catch and handle the exception
     */

    explicit SharedLibLoader(LPCTSTR pstrFilename) :

#ifdef _WIN32
        m_hModule(LoadLibraryEx( TEXT(pstrFilename), NULL, LOAD_WITH_ALTERED_SEARCH_PATH ))
#else
        m_hModule(dlopen(pstrFilename, RTLD_NOW))
#endif
    {
        if( nullptr == m_hModule )
        {
#ifdef _WIN32
            throw std::system_error(EDOM, std::generic_category(), (std::string("Failed to load shared library: ") + pstrFilename + uerror::getLastError()));
#else
            throw std::system_error(EDOM, std::generic_category(), (std::string("Failed to load shared library: ") + pstrFilename));
#endif //_WIN32
        }
    }

    /*
     * \brief The class destructor
     * \note Unload the shared library
     */

    ~SharedLibLoader()
    {
#ifdef _WIN32
        FreeLibrary(m_hModule);
#else
        dlclose(m_hModule);
#endif
    }


    /*
     * \brief overload operator []
     * \note Returns the address of the symbol specified as string parameter
     */

    ProcAddress operator[] ( LPCSTR pstrProcName ) const
    {
#ifndef _WIN32
        // forced call to cleanup the older error conditions
        dlerror();
#endif
        FARPROC procPtr =
#ifdef _WIN32
            GetProcAddress(m_hModule, pstrProcName);
#else
            dlsym(m_hModule, pstrProcName);
#endif

        if ( nullptr == procPtr )
        {

#ifdef _WIN32
            throw std::system_error( EDOM, std::generic_category(), (std::string("Failed to load symbol:") + std::string(pstrProcName) + uerror::getLastError()));
#else
            char *pstrError = dlerror();
            throw std::system_error( EDOM, std::generic_category(), (std::string("Failed to load symbol:") + std::string(pstrProcName) + std::string(". Error:") + std::string((nullptr != pstrError) ? pstrError : "?" )));
#endif
        }

        return ProcAddress(procPtr);
    }

private:

      HMODULE m_hModule;

};

#endif // USHAREDLIBLOADER_HPP