// Internal (not installed) helpers shared by the two C-ABI translation units: the runtime half
// (vasset_c_api_runtime.cpp, in the `vasset` target) and the import half (vasset_c_api_import.cpp,
// in the `vasset-import` target, which links `vasset`). The single definition of the per-thread
// last-error store lives in the runtime TU so vasset_last_error() reports failures from both halves.
#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_c_api.h"

#include <string>

namespace vasset::capi
{
    extern thread_local std::string g_lastError;

    void    setLastError(std::string msg);
    int32_t fail(VAssetStatus code, const char* msg);        // returns -code, sets last error
    int32_t failAsset(vasset::AssetError e, const char* ctx); // returns -(AssetError), sets last error
} // namespace vasset::capi
