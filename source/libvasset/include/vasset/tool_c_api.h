#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct VAssetToolArgs
{
    uint32_t struct_size;
    int      argc;
    char**   argv;
} VAssetToolArgs;

int vasset_tool_run_cli(const VAssetToolArgs* args);

#ifdef __cplusplus
}
#endif
