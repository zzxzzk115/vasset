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
    struct VSkeleton
    {
        vbase::UUID uuid;
        std::string name;

        std::vector<std::string> jointNames;
        std::vector<int16_t>     jointParents;

        std::vector<std::byte> ozzData;

        std::string sourceFileName;
    };

    struct VAnimation
    {
        vbase::UUID uuid;
        std::string name;
        float       duration {0.0f};

        std::vector<std::byte> ozzData;

        std::string sourceFileName;
    };

    vbase::Result<void, AssetError> saveSkeleton(const VSkeleton& skeleton, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadSkeleton(vbase::StringView filePath, VSkeleton& outSkeleton);
    vbase::Result<void, AssetError> loadSkeletonFromMemory(const std::vector<std::byte>& data, VSkeleton& outSkeleton);

    vbase::Result<void, AssetError> saveAnimation(const VAnimation& animation, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadAnimation(vbase::StringView filePath, VAnimation& outAnimation);
    vbase::Result<void, AssetError> loadAnimationFromMemory(const std::vector<std::byte>& data, VAnimation& outAnimation);
} // namespace vasset
