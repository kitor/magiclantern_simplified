#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

#if CONFIG_FW_VERSION == 180

struct region_patch mmu_data_patches[] =
{
};

struct function_hook_patch mmu_code_patches[] =
{
};

#endif // 200D FW_VERSION 101

#endif // __PLATFORM_MMU_PATCHES_H__
