#pragma once

#include <vbase/core/string_view.hpp>

namespace vasset
{
    struct VImport
    {};

    bool saveVImport(const VImport& import, vbase::StringView filePath);
    bool loadVImport(vbase::StringView filePath, VImport& outImport);
} // namespace vasset