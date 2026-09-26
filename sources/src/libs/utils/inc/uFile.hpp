#ifndef UFILE_HPP
#define UFILE_HPP

#include "uLogger.hpp"

#include <filesystem>
#include <string_view>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "FILE_OPS    |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace ufile {

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Check if a file exists and is not empty
     *
     */
    /*--------------------------------------------------------------------------------------------------------*/

    inline bool fileExistsAndNotEmpty(const std::string &strPath)
    {
        namespace fs = std::filesystem;
        try {
            return fs::exists(strPath) && fs::is_regular_file(strPath) && fs::file_size(strPath) > 0;
        } catch (const fs::filesystem_error &) {
            return false;
        }
    }

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Check if a file exists and is not empty (string_view overload)
     *
     * std::filesystem::path is constructible directly from a string_view (no
     * intermediate std::string needed), so callers that already hold a view
     * (e.g. a decorator-stripped filename inside a larger owned string) don't
     * have to allocate just to make this call.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    inline bool fileExistsAndNotEmpty(std::string_view path)
    {
        namespace fs = std::filesystem;
        try {
            fs::path p(path);
            return fs::exists(p) && fs::is_regular_file(p) && fs::file_size(p) > 0;
        } catch (const fs::filesystem_error &) {
            return false;
        }
    }

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Build a file pathname out of path and name and return it directly
     *
     */
    /*--------------------------------------------------------------------------------------------------------*/

    inline std::string buildFilePath(const std::string &strDir, const std::string &strFilename)
    {
        std::filesystem::path fullPath = strDir;
        fullPath /= strFilename;
        return fullPath.string();
    }

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Build a file pathname out of path and name and return it via an output parameter
     *
     */
    /*--------------------------------------------------------------------------------------------------------*/

    inline void buildFilePath(const std::string &strDir, const std::string &strFilename, std::string &strOutPath)
    {
        std::filesystem::path fullPath = strDir;
        fullPath /= strFilename;
        strOutPath = fullPath.string();
    }

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Get the size of a file and return it directly
     *
     */
    /*--------------------------------------------------------------------------------------------------------*/

    inline std::uintmax_t getFileSize(const std::string &strFilePath)
    {
        try {
            return std::filesystem::file_size(strFilePath);
        } catch (const std::filesystem::filesystem_error &e) {
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

    inline bool getFileSize(const std::string &strFilePath, std::uintmax_t &sizeOut)
    {
        try {
            sizeOut = std::filesystem::file_size(strFilePath);
            return true;
        } catch (const std::filesystem::filesystem_error &e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error getting file size:"); LOG_STRING(e.what()));
            sizeOut = 0;
            return false;
        }
    }

} // namespace ufile

#endif // UFILE_HPP