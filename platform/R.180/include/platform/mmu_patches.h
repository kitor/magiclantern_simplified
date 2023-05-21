#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

static const unsigned char earl_grey_str[] = "Oh, hi!         ";
static const unsigned char engage_str[] = "Is it working?     ";

#if CONFIG_FW_VERSION == 180 // ensure our hard-coded patch addresses are not broken
                             // by a FW upgrade
struct region_patch mmu_data_patches[] =
{
    {
        // replace "Dust Delete Data" with "Earl Grey, hot",
        // as a low risk (non-code) test that MMU remapping works.
        .patch_addr = 0xf0c422f0,
        .orig_content = NULL,
        .patch_content = earl_grey_str,
        .size = sizeof(earl_grey_str),
        .description = "Tea"
    },
    {
        // replace "High ISO speed NR" with "Engage!",
        // as a low risk (non-code) test that MMU remapping works.
        .patch_addr = 0xf0c3b3af,
        .orig_content = NULL,
        .patch_content = engage_str,
        .size = sizeof(engage_str),
        .description = "GO!"
    }
};

struct function_hook_patch mmu_code_patches[] =
{

};

#endif // EOSR FW_VERSION 180

#endif // __PLATFORM_MMU_PATCHES_H__
