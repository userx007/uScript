#ifndef IDATATYPES_HPP
#define IDATATYPES_HPP

#include <memory>
#include <functional>
#include <span>
#include <cstdint>

enum class ReadType
{
    TOKEN,
    LINE,
    DEFAULT
};

template<typename TDriver>
using PFSEND = std::function<bool(std::span<const uint8_t>, std::shared_ptr<const TDriver>)>;

template<typename TDriver>
using PFRECV = std::function<bool(std::span<uint8_t>, size_t& szSize, ReadType readType, std::shared_ptr<const TDriver>)>;


#endif // IDATATYPES_HPP