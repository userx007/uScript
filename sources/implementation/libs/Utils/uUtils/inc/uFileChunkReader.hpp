#ifndef UFILE_CHUNKREADER_H
#define UFILE_CHUNKREADER_H

#include <string>
#include <functional>
#include <string_view>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <span>
#include <cstddef>
#include <algorithm>


#if defined(_WIN32)
    #ifdef _MSC_VER
        #define NOMINMAX
    #endif /* _MSC_VER */
    #include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

/////////////////////////////////////////////////////////////////////////////////
//                             LOG DEFINITIONS                                 //
/////////////////////////////////////////////////////////////////////////////////


#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FCHUNKREAD :"
#define LOG_HDR    LOG_STRING(LT_HDR); LOG_STRING(__FUNCTION__)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

namespace ufile
{

template <typename TDriver>
class FileChunkReader
{
    public:

        using ChunkHandler = std::function<bool(std::span<const uint8_t>, std::shared_ptr<const TDriver>)>;

        static bool read(const std::string& filename, std::size_t chunkSize, const ChunkHandler& handler, std::shared_ptr<const TDriver> shpDriver)
        {
            if(!handler){
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Callback not provided!"));
                return false;
            }

            if(0 == chunkSize){
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid chunksize (0)"));
                return false;
            }

#if defined(_WIN32)
            return readWindows(filename, chunkSize, handler, shpDriver);
#elif defined(__unix__) || defined(__APPLE__)
            return readPosix(filename, chunkSize, handler, shpDriver);
#else
            return readFallback(filename, chunkSize, handler, shpDriver);
#endif
        } /* read() */

    private:

#if defined(_WIN32)

        static bool readWindows(const std::string& filename, std::size_t chunkSize, const ChunkHandler& handler, std::shared_ptr<const TDriver> shpDriver)
        {
            HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (hFile == INVALID_HANDLE_VALUE) {
                return false;
            }

            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(hFile, &fileSize)) {
                CloseHandle(hFile);
                return false;
            }

            HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!hMap) {
                CloseHandle(hFile);
                return false;
            }

            void* data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
            if (!data) {
                CloseHandle(hMap);
                CloseHandle(hFile);
                return false;
            }

            const uint8_t* ptr = static_cast<const uint8_t*>(data);
            std::size_t size = static_cast<std::size_t>(fileSize.QuadPart);

            for (std::size_t offset = 0; offset < size; offset += chunkSize) {
                std::size_t len = std::min(chunkSize, size - offset);
                if (!handler(std::span<const uint8_t>(ptr + offset, len), shpDriver)) {
                    break;
                }
            }

            UnmapViewOfFile(data);
            CloseHandle(hMap);
            CloseHandle(hFile);
            return true;

        } /* readWindows() */

#endif //  defined(_WIN32)

#if defined(__unix__) || defined(__APPLE__)

        static bool readPosix(const std::string& filename, std::size_t chunkSize, const ChunkHandler& handler, std::shared_ptr<const TDriver> shpDriver)
        {
            int fd = open(filename.c_str(), O_RDONLY);

            if (fd == -1) {
                return false;
            }

            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                close(fd);
                return false;
            }

            std::size_t size = static_cast<std::size_t>(sb.st_size);
            if (size == 0) {
                close(fd);
                return true;
            }

            void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped == MAP_FAILED) {
                close(fd);
                return false;
            }

            const uint8_t* ptr = static_cast<const uint8_t*>(mapped);
            for (std::size_t offset = 0; offset < size; offset += chunkSize) {
                std::size_t len = std::min(chunkSize, size - offset);
                if (!handler(std::span<const uint8_t>(ptr + offset, len), shpDriver)) {
                    break;
                }
            }

            munmap(mapped, size);
            close(fd);
            return true;

        } /* readPosix() */

#endif // defined(__unix__) || defined(__APPLE__)

        static bool readFallback(const std::string& filename, std::size_t chunkSize, const ChunkHandler& handler, std::shared_ptr<const TDriver> shpDriver)
        {
            std::ifstream file(filename, std::ios::binary);

            if (!file) {
                return false;
            }

            std::vector<uint8_t> buffer(chunkSize);
            while (file) {
                file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
                std::streamsize bytesRead = file.gcount();
                if (bytesRead > 0) {
                    if (!handler(std::span<const uint8_t>(buffer.data(), static_cast<std::size_t>(bytesRead)), shpDriver)) {
                        break;
                    }
                }
            }
            return true;

        } /* readFallback() */
};

} // namespace ufile

#endif // UFILE_CHUNKREADER_H