#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace vasset
{
    struct VUUID
    {
        std::array<uint8_t, 16> bytes {};

        static VUUID generate();

        static VUUID fromString(const std::string& str);

        std::string toString() const;

        bool operator==(const VUUID& other) const noexcept;
    };

} // namespace vasset

namespace std
{
    template<>
    struct hash<vasset::VUUID>
    {
        size_t operator()(const vasset::VUUID& id) const noexcept
        {
            const uint64_t* p = reinterpret_cast<const uint64_t*>(id.bytes.data());
            return std::hash<uint64_t>()(p[0]) ^ std::hash<uint64_t>()(p[1]);
        }
    };
} // namespace std