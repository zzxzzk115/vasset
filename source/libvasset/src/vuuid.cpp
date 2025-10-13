#include "vasset/vuuid.hpp"

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
        VUUID             id {};
        std::stringstream ss(str);
        for (int i = 0; i < 16; ++i)
        {
            unsigned int value;
            ss >> std::hex >> value;
            id.bytes[i] = static_cast<uint8_t>(value);
            if (ss.peek() == '-' || ss.peek() == ' ')
                ss.ignore();
        }
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

    bool VUUID::operator==(const VUUID& other) const noexcept { return bytes == other.bytes; }
} // namespace vasset