#ifndef UFILE_HPP
#define UFILE_HPP

#include "uLogger.hpp"

#include <filesystem>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FILE       :"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////


namespace ufile
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Check if a file exists and is not empty
 *
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool fileExistsAndNotEmpty(const std::string& path)
{
    namespace fs = std::filesystem;
    try {
        return fs::exists(path) && fs::is_regular_file(path) && fs::file_size(path) > 0;
    } catch (const fs::filesystem_error&) {
        return false;
    }
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Build a file pathname out of path and name and return it directly
 *
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::string buildFilePath(const std::string& dir, const std::string& filename)
{
    std::filesystem::path fullPath = dir;
    fullPath /= filename;
    return fullPath.string();
}



/*--------------------------------------------------------------------------------------------------------*/
/**
* @brief Build a file pathname out of path and name and return it via an output parameter
 *
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void buildFilePath(const std::string& dir, const std::string& filename, std::string& outPath)
{
    std::filesystem::path fullPath = dir;
    fullPath /= filename;
    outPath = fullPath.string();
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Get the size of a file and return it directly
 *
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::uintmax_t getFileSize(const std::string& filePath)
{
    try {
        return std::filesystem::file_size(filePath);
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error getting file size:"); LOG_STRING(e.what()));
        return 0;
    }
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Get the size of a file and return it via an output parameter
 *
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool getFileSize(const std::string& filePath, std::uintmax_t& sizeOut)
{
    try {
        sizeOut = std::filesystem::file_size(filePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error getting file size:"); LOG_STRING(e.what()));
        sizeOut = 0;
        return false;
    }
}


} // namespace ufile

#endif // UFILE_HPP