#include "vasset/vuuid.hpp"

#include <xxhash.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace vasset
{
    VUUID VUUID::generate()
    {
        VUUID                     id {};
        static std::random_device rd;
        static std::mt19937_64    eng(rd());
        for (auto& b : id.bytes)
            b = static_cast<uint8_t>(eng() & 0xFF);
        return id;
    }

    VUUID VUUID::fromString(const std::string& str)
    {
        VUUID       id {};
        std::string hexStr;

        for (char c : str)
        {
            if (std::isxdigit(static_cast<unsigned char>(c)))
                hexStr += c;
        }

        if (hexStr.size() != 32)
        {
            return id;
        }

        for (size_t i = 0; i < 16; ++i)
        {
            std::string byteStr = hexStr.substr(i * 2, 2);
            id.bytes[i]         = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        }

        return id;
    }

    VUUID VUUID::fromFilePath(const std::string& filePath)
    {
        std::string normalized = filePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

        XXH128_hash_t h = XXH3_128bits(normalized.data(), normalized.size());
        VUUID         id;
        std::memcpy(id.bytes.data(), &h, sizeof(h));
        return id;
    }

    VUUID VUUID::fromName(const std::string& str)
    {
        XXH128_hash_t h = XXH3_128bits(str.data(), str.size());
        VUUID         id;
        std::memcpy(id.bytes.data(), &h, sizeof(h));
        return id;
    }

    std::string VUUID::toString() const
    {
        std::ostringstream ss;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
            if ((i == 3) || (i == 5) || (i == 7) || (i == 9))
                ss << "-";
        }
        return ss.str();
    }

    bool VUUID::isNil() const
    {
        return std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0; });
    }

    bool VUUID::operator==(const VUUID& other) const noexcept { return bytes == other.bytes; }

    bool VUUID::operator!=(const VUUID& other) const noexcept { return !(*this == other); }

    bool VUUID::operator<(const VUUID& other) const noexcept
    {
        return std::lexicographical_compare(bytes.begin(), bytes.end(), other.bytes.begin(), other.bytes.end());
    }
} // namespace vasset