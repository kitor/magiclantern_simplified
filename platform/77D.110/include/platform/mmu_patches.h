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


/*

void __attribute__((noreturn,noinline,naked,aligned(4)))set_hw_layer_params(void)
{
    asm(
        "push { r0-r11, lr }\n"
    );

    uint32_t layer, mask, data;
    asm __volatile__ (
        "mov %0, r0\n"
        "mov %1, r1\n"
        "mov %2, r2\n": "=&r"(layer), "=&r"(mask), "=&r"(data)
    );

  //  if((layer == 7) && ((mask & 0x20000) || (mask & 0x40000))){
    if(layer == 7){
//        DryosDebugMsg(0, 15, " ==== set_hdmi_hw_layer_params 7");
//        DryosDebugMsg(0, 15, "layer %08x", layer);
//        DryosDebugMsg(0, 15, "mask %08x", mask);
//        DryosDebugMsg(0, 15, "data %08x", data);
        asm(
            "pop { r0-r11, lr }\n"

            // do overwritten instructions
            "push       { r4, r5, r6, lr }\n"
            "ldr        r6, =0x60000\n"   // force HWLAYER_DOUBLE_H | HWLAYER_DOUBLE_V
            "ldr        r5, =0x0\n"   // update mask to cover both bits
            "mov        r4, r0\n"

            // jump back
            "ldr pc, =0xe0572833\n"
        );
    }
    else
    {
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
} */

/*
void __attribute__((noreturn,noinline,naked,aligned(4)))set_hdmi_scaling_params(void)
{
    asm(
        "push { r0-r11, lr }\n"
    );

    uint32_t layer, flags;
    asm __volatile__ (
        "mov %0, r0\n"
        "mov %1, r1\n": "=&r"(layer), "=&r"(flags)
    );

  //  if((layer == 7) && ((mask & 0x20000) || (mask & 0x40000))){
    if(layer == 7){
        DryosDebugMsg(0, 15, " ==== set_hdmi_scaling_params 7");
        DryosDebugMsg(0, 15, "layer %08x", layer);
        DryosDebugMsg(0, 15, "flags %08x", flags);
    }
    asm(
        "pop { r0-r11, lr }\n"

        // do overwritten instructions
        "push       { r4, r5, r6, lr }\n"
        "mov        r6, r2\n"
        "mov        r5, r1\n"
        "mov        r4, r0\n"

        // jump back
        "ldr pc, =0xe0571979\n"
    );
}  */

struct function_hook_patch early_code_patches[] =
{
/*    {
      .patch_addr = 0xe057282a,
      .orig_content = {0x70, 0xB5, 0x16, 0x46, 0x0D, 0x46, 0x04, 0x46},
      .target_function_addr = (uint32_t)set_hw_layer_params,
      .description = "Log setting HDMI HW layers flags"
    } */
/*    {
      .patch_addr = 0xe0571970,
      .orig_content = {0x70, 0xB5, 0x07, 0x28, 0x0D, 0x46, 0x04, 0x46},
      .target_function_addr = (uint32_t)set_hdmi_scaling_params,
      .description = "Log setting HDMI HW layers flags"
    } */
};

struct function_hook_patch normal_code_patches[] =
{


};

#endif // 77D FW_VERSION 110

#endif // __PLATFORM_MMU_PATCHES_H__
