#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    // Source container of the cooked font payload. The runtime hands fontData straight
    // to FreeType (FT_New_Memory_Face), which sniffs the face from the bytes, so the
    // format is informational metadata rather than something the loader depends on.
    enum class VFontFormat : uint32_t
    {
        eUnknown = 0,
        eTTF,
        eOTF,
    };

    inline std::string toString(VFontFormat format)
    {
        switch (format)
        {
            case VFontFormat::eTTF:
                return "ttf";
            case VFontFormat::eOTF:
                return "otf";
            default:
                return "unknown";
        }
    }

    struct VFont
    {
        vbase::UUID uuid;
        std::string name;

        VFontFormat format {VFontFormat::eTTF};

        // Raw .ttf/.otf bytes. Font cooking is a verbatim copy: FreeType reads these directly.
        std::vector<std::byte> fontData;

        std::string sourceFileName;
    };

    vbase::Result<void, AssetError> saveFont(const VFont& font, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadFont(vbase::StringView filePath, VFont& outFont);
    vbase::Result<void, AssetError> loadFontFromMemory(const std::vector<std::byte>& data, VFont& outFont);
} // namespace vasset
