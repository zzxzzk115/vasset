#pragma once

#include "vasset/vasset_importers.hpp"

namespace vasset::tool
{
    int run_vasset_cli(int argc, char** argv);
    int run_vasset_cli(int argc, char** argv, const VAssetImporter::ImportOptions& importOptions);
}
