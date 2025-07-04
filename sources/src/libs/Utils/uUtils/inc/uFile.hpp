#ifndef UFILE_HPP
#define UFILE_HPP

#include <iostream>
#include <filesystem>

namespace ufile
{

    inline bool fileExistsAndNotEmpty(const std::string& path)
    {
        namespace fs = std::filesystem;
        return fs::exists(path) && fs::is_regular_file(path) && fs::file_size(path) > 0;
    }


    inline std::string buildFilePath(const std::string& dir, const std::string& filename)
    {
        std::filesystem::path fullPath = std::filesystem::path(dir) / filename;
        return fullPath.string();
    }


    inline void buildFilePath(const std::string& dir, const std::string& filename, std::string& outPath)
    {
        std::filesystem::path fullPath = std::filesystem::path(dir) / filename;
        outPath = fullPath.string();
    }


} //UFILE_HPP

#endif //UFILE_HPP