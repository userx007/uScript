#ifndef SHLIBIFENTRY_WINDOWS_HPP
#define SHLIBIFENTRY_WINDOWS_HPP

#include "shlibifdecl_windows.hpp"
#include "uSharedLibLoader.hpp"

#define LIBUSBK_DLL_NAME  "libusbK.dll"

////////////////////////////////////////////////////////////////////////////////////////////
//
//  Class used to get entry points of some functions of libusbK.dll
//
////////////////////////////////////////////////////////////////////////////////////////////

class LibUsbKApi
{
    /////////////////////////////////////////////
    // shared library handler
    /////////////////////////////////////////////

    SharedLibLoader shlib{ LIBUSBK_DLL_NAME };

public:

    /////////////////////////////////////////////
    // symbol address handler
    /////////////////////////////////////////////

    decltype(ExtIF_LoadDrvApi)    *pfLoadDrvApi    = shlib["LibK_LoadDriverAPI"];
    decltype(ExtIF_LstInit)       *pfLstInit       = shlib["LstK_Init"];
    decltype(ExtIF_LstCount)      *pfLstCount      = shlib["LstK_Count"];
    decltype(ExtIF_LstFree)       *pfLstFree       = shlib["LstK_Free"];
    decltype(ExtIF_LstEnumerate)  *pfLstEnumerate  = shlib["LstK_Enumerate"];
    decltype(ExtIF_LstMoveNext)   *pfLstMoveNext   = shlib["LstK_MoveNext"];
    decltype(ExtIF_LstMoveReset)  *pfLstMoveReset  = shlib["LstK_MoveReset"];

};

#endif // SHLIBIFENTRY_WINDOWS_HPP