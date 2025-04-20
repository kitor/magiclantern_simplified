#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

#if CONFIG_FW_VERSION == 110 // ensure our hard-coded patch addresses are not broken
                             // by a FW upgrade
struct patch early_data_patches[] =
{
};

struct patch normal_data_patches[] =
{
};

void __attribute__((noreturn,noinline,naked,aligned(4)))set_hw_layer_params(void)
{
    asm(
        "push { r0-r11, lr }\n"
    );

    uint32_t a0, a1, a2;
    asm __volatile__ (
        "mov %0, r0\n"
        "mov %1, r1\n"
        "mov %2, r2\n": "=&r"(a0), "=&r"(a1), "=&r"(a2)
    );

    DryosDebugMsg(0, 15, " ==== set_hdmi_hw_layer_params");
    DryosDebugMsg(0, 15, "arg1 %08x", a0);
    DryosDebugMsg(0, 15, "arg1 %08x", a1);
    DryosDebugMsg(0, 15, "arg1 %08x", a2);
    asm(
        "pop { r0-r11, lr }\n"

        // do overwritten instructions
        "push       { r4, r5, r6, lr }\n"
        "mov        r6, r2\n"
        "mov        r5, r1\n"
        "mov        r4, r0\n"

        // jump back
        "ldr pc, =0xe0572833\n"
    );
}

struct function_hook_patch early_code_patches[] =
{
/*    {
      .patch_addr = 0xe057282a,
      .orig_content = {0x70, 0xB5, 0x16, 0x46, 0x0D, 0x46, 0x04, 0x46},
      .target_function_addr = (uint32_t)set_hw_layer_params,
      .description = "Log setting HDMI HW layers flags"
    } */
};

struct function_hook_patch normal_code_patches[] =
{


};

#endif // 77D FW_VERSION 110

#endif // __PLATFORM_MMU_PATCHES_H__
