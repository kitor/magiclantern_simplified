/** \file
 * Temporary tests for features, specific for R 1.8.0
 *
 * When porting to a new model, using this camera as a base, remove this file
 * and remove it from ML_SRC_EXTRA_OBJS in Makefile.platform.default!
 */
/*
 * Copyright (C) 2022 Magic Lantern Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef CONFIG_HELLO_WORLD
#include <dryos.h>
#include <edmac.h>
#include <edmac-memcpy.h>

extern void GUI_SetImgComposition(int card,int jpg,int fmt,int jpgq,int rawq);

static void raw_enable()
{
    msleep(500); //
    GUI_SetImgComposition(1,0,6,3,4);
    GUI_SetImgComposition(2,0,6,3,4);
}

extern uint32_t mem2mem_emdac_copy_d8(void * src, void * dst, struct edmac_info * src_info, struct edmac_info * dst_info);

static void test_edmac_memcpy()
{
    DryosDebugMsg(0, 15, "Attempting EDMAC mem->mem copy");

    // create test buffers
    //
    // Get 256k aligned to 4k, we will split into two
    // EDMAC copies do have some alignment requirements on the buffers,
    // I don't know what they are yet, but apparently less than 4k.
    uint32_t slab_size = 1 << 18; // 256kB
    uint32_t region_size = slab_size / 2;
    uint8_t *slab = malloc_aligned(slab_size, 0x1000);

    // below 4MB test with unused R region
    //uint32_t slab_size = 1 << 22; // 4MB
    //uint32_t region_size = slab_size / 2;
    //uint8_t *slab = (uint8_t *)0x9D9A0000; // probably unused shoot mem
    if (slab == NULL)
    {
        DryosDebugMsg(0, 15, "Failed to get aligned slab");
        return;
    }
    uint8_t *dst = slab;
    uint8_t *src = slab + region_size;

    DryosDebugMsg(0, 15, "region_size: 0x%x", region_size);
    DryosDebugMsg(0, 15, "dst, src: 0x%x, 0x%x", dst, src);

    // Use Uncacheable addresses, and flush read cache before read.
    // Unsure if required, but old ML code does this and I've only
    // observed Uncache addresses being used on 200d.
    dst = UNCACHEABLE(dst);
    src = UNCACHEABLE(src);

    DryosDebugMsg(0, 15, "uncached: 0x%x, 0x%x", dst, src);

    // initialise content.  The markers allow
    // detecting overwrites when memcmp'ing the regions.
    memset(dst, 0x77, region_size);
    memset(dst, 0x12, 2); // start marker
    memset(dst + region_size - 2, 0x34, 2); // end marker

    memset(src, 0xa5, region_size);
    memset(src, 0xAB, 2); // start marker
    memset(src + region_size - 2, 0xCD, 2); // end marker
    sync_caches();

    DryosDebugMsg(0, 15, "Pre-copy, *dst, *src: 0x%x, 0x%x",
                  *(uint32_t *)dst,
                  *(uint32_t *)src);

    uint32_t div = 128; // this works for around 4MB. It seems .xb limit is around 32-64k
    struct edmac_info edmacInfo = {
      .xb = region_size/div,
      .yb = div-1
    };

    int before, after;
    before = get_ms_clock();

    edmac_memcpy_res_lock();
    // below is what edmac_copy_rectangle_cbr_start calls,
    // still needs to be wrapped by res_lock/unlock
    uint32_t err = mem2mem_emdac_copy_d8(src, dst, &edmacInfo, &edmacInfo);
    edmac_memcpy_res_unlock();

    after = get_ms_clock();

    if(err)
    {
        DryosDebugMsg(0, 15, "EDMAC transfer failed");
    }
    else
    {
        DryosDebugMsg(0, 15, "Post-copy, *dst, *src: 0x%x, 0x%x",
                      *(uint32_t *)dst,
                      *(uint32_t *)src);
        DryosDebugMsg(0, 15, "ms time for size 0x%x: %d", region_size, after - before);
        if (memcmp(dst, src, region_size) != 0)
            DryosDebugMsg(0, 15, "dst / src content mismatch!");
        else
            DryosDebugMsg(0, 15, "dst / src content equal :)");
    }
    free_aligned(slab);
    slab = NULL;
    dst = NULL;
    src = NULL;
}

static struct menu_entry test_features_debug_menu[] = {
    {
        .name   = "SX740 Test features",
        .select = menu_open_submenu,
        .help   = "Temporary features, not integrated into main codebase yet.",
        .children =  (struct menu_entry[]) {
            {
                .name   = "test edmac memcpy",
                .priv   = test_edmac_memcpy,
                .select = run_in_separate_task,
                .help   = "edmac memcopy test"
            },
            {
                .name   = "Enable CR3 RAW",
                .priv   = raw_enable,
                .select = run_in_separate_task,
                .help   = "HIGHLY EXPERIMENTAL! Revert back using Canon menu."
            },
            MENU_EOL,
        },
    },
};

static void test_features_init()
{
    menu_add("Debug", test_features_debug_menu, COUNT(test_features_debug_menu));
}

INIT_FUNC(__FILE__, test_features_init);

#endif
