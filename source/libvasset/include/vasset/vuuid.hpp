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

        static VUUID fromFilePath(const std::string& filePath);

        static VUUID fromName(const std::string& str);

        std::string toString() const;

        bool isNil() const;

        bool operator==(const VUUID& other) const noexcept;
        bool operator!=(const VUUID& other) const noexcept;
        bool operator<(const VUUID& other) const noexcept;
    };

} // namespace vasset

namespace std
{
    template<>
    struct hash<vasset::VUUID>
    {
        size_t operator()(const vasset::VUUID& id) const noexcept { return std::hash<std::string> {}(id.toString()); }
    };
} // namespace std