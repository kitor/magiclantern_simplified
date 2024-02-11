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

extern void DISP_SetHighLight(int);
int highlight_flag = 0;

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
                  
    uint32_t err = mem2mem_emdac_copy_d8(src, dst, &edmacInfo, &edmacInfo);
    
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


// DmacInfo array, contains pointers to EDMAC channel MMIO
#define MAX_CHANNELS 76
struct DmacInfoEntry
{
    struct edmac_mmio * pEdmacChannel;
    uint32_t ModeInfo;
};
struct DmacInfoEntry* DmacInfo = (struct DmacInfoEntry *)0xe0dd5c64;

// InterruptHandlers array - each entry sets Interrupt ID and CBR for Nth channel
struct InterruptHandlerEntry
{
    uint32_t id;
    uint32_t cbr;
};
struct InterruptHandlerEntry* Handlers = (struct InterruptHandlerEntry*)0xE0DD641C;

// InterrutpID to description map. 512 entries on R
char** IVT = (char **)0x19390;

/*
 * List of subchips to wake up
 * On R (D8?) shamem_read is not used - all reads/writes are done directly
 * via edmac_mmio.
 *
 * Caveat is whenever I tried to read or write in `EDOMAIN_EDMAC_5_*` camera locked.
 * I discovered that if I wake subchip 4 (aka N-1) - it works fine.
 *
 * Canon code always wraps EDMAC stuff into Wake/Suspend calls, thus we need
 * to follow this dance.
 */
const uint32_t devs[] = {0,1,2,3,4,5,6,7};
extern void PwrMng_WakeSubChips(const uint32_t *list);
extern void PwrMng_SuspendSubChips(const uint32_t *list);
extern uint32_t dump_file(char *name,void *buf,size_t size);
/*
 * Dump all EDMAC channels that have params set
 *
 * As for now we don't know which mmio reg is "dma in progress" flag
 */
static void watch_edmac_channels()
{
    PwrMng_WakeSubChips(devs);
    uint32_t xa, xb, xn, ya, yb, yn, xs, ys, w, h;
    for(int i = 0; i < MAX_CHANNELS; i++)
    {
        struct edmac_mmio * ch = DmacInfo[i].pEdmacChannel;
        char * name = IVT[Handlers[i].id];
        if(!ch->yb_xb)
          continue; // skip unset channels... does every call use yb/xb?
        yn = ch->yn_xn >> 16;
        xn = ch->yn_xn & 0xFFFF;
        yb = ch->yb_xb >> 16;
        xb = ch->yb_xb & 0xFFFF;
        ya = ch->ya_xa >> 16;
        xa = ch->ya_xa & 0xFFFF;
        xs = ch->ys_xs >> 16;
        ys = ch->ys_xs & 0xFFFF;
        w = xa * xn + xb;
        h = ya * yn + yb;
        DryosDebugMsg(0, 15, "CH %02d: %08x %08x %s", i, ch, ch->ram_addr,  name);
        DryosDebugMsg(0, 15, "    x %d*%d+%d y %d*%d+%d = %dx%d", xn, xa, xb, yn, ya, yb, w, h);
        DryosDebugMsg(0, 15, "    xs %d ys %d off3 %d", xs, ys, ch->off3);
        if(ch->ram_addr)
        {
            char name [24];
            snprintf(name, sizeof(name), "%d.dump", i);
            dump_file(name, ch->ram_addr, w*h);
        }
        //DryosDebugMsg(0, 15, "    p %08x",  ch->PackUnpackInfo);
    }
    PwrMng_SuspendSubChips(devs);
}

static void overexpo_toggle()
{
    highlight_flag = !highlight_flag;
    msleep(1000); //let the code execute when user is already back in LV
    DryosDebugMsg(0, 15, "Toggle overexposure warning in LV: %d", highlight_flag);
    DISP_SetHighLight(highlight_flag);
}

static struct menu_entry test_features_debug_menu[] = {
    {
        .name   = "EOS R Test features",
        .select = menu_open_submenu,
        .help   = "Temporary features, not integrated into main codebase yet.",
        .children =  (struct menu_entry[]) {
            {
                .name   = "watch edmac channels",
                .priv   = watch_edmac_channels,
                .select = run_in_separate_task,
                .help   = "watch edmac channels"
            },
            {
                .name   = "Overexposure warning in LiveView",
                .priv   = overexpo_toggle,
                .select = run_in_separate_task,
                .help   = "You may need to toggle twice each time you enter LV."
            },
            {
                .name   = "test edmac memcpy",
                .priv   = test_edmac_memcpy,
                .select = run_in_separate_task,
                .help   = "edmac memcopy test"
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
