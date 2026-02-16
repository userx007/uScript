#ifndef UERROR_HANDLING_HPP
#define UERROR_HANDLING_HPP

#include <string>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace uerror
{

#ifdef _WIN32
/*
 * \brief Get the last windows error as string
 * \param none
 * \return string corresponding to the error
*/

inline std::string getLastError()
{
    DWORD dwErrorMessageID = ::GetLastError();
    std::string strErrCode = " [" + std::to_string(dwErrorMessageID) + "] ";

    LPSTR lpstrMessageBuffer = nullptr;
    size_t szSize = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, dwErrorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpstrMessageBuffer, 0, NULL);

    std::string strMessage;
    if (szSize > 0 && lpstrMessageBuffer != nullptr) {
        strMessage.assign(lpstrMessageBuffer, szSize);
        strMessage.erase(std::remove(strMessage.begin(), strMessage.end(), '\n'), strMessage.end());
        strMessage.erase(std::remove(strMessage.begin(), strMessage.end(), '\r'), strMessage.end());
        LocalFree(lpstrMessageBuffer);
    } else {
        strMessage = "Unknown error";
    }

    return strErrCode + strMessage;
}
#endif // _WIN32

} // namespace uerror

#endif //UERROR_HANDLING_HPP