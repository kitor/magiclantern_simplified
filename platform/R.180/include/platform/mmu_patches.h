#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

#if CONFIG_FW_VERSION == 180

struct patch early_data_patches[] =
{
};

//static uint8_t test_str[] = "harlem shake (%#x)";
struct patch normal_data_patches[] =
{
/*    {
        .addr = (uint8_t *)0xe0041788,
        .old_values = (uint8_t *)0xe0041788,
        .new_values = test_str,
        .size = sizeof(test_str),
        .description = "test"
    }, */
};

void __attribute__((noreturn,noinline,naked,aligned(4)))log_highlights_params(void)
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

  DryosDebugMsg(0, 15, " ==== SetHighLightParameter params");
  DryosDebugMsg(0, 15, "arg1 %08x", a0);
  DryosDebugMsg(0, 15, "arg1 %08x", a1);
  DryosDebugMsg(0, 15, "arg1 %08x", a2);
    asm(
        "pop { r0-r11, lr }\n"

        // do overwritten instructions
        "push {r4, lr}\n"
        "mov  r4, r2\n"
        "mov  r3, r2\n"
        "movs r1, #3\n"

        // jump back
        "ldr pc, =0xe05176e1\n"
    );
}

struct function_hook_patch early_code_patches[] =
{
};

struct function_hook_patch normal_code_patches[] =
{
    {
        .patch_addr = 0xe05176d8,
        .orig_content = {0x10, 0xb5, 0x14, 0x46, 0x13, 0x46, 0x03, 0x21},
        .target_function_addr = (uint32_t)log_highlights_params,
        .description = "Log SetHighLightParameter params"
    },
};

#endif // R FW_VERSION 180

#endif // __PLATFORM_MMU_PATCHES_H__
