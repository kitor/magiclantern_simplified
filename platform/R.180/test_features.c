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


/*
 * On D8 those take a list of subchips (0-6), terminated with 7.
 * Those seem to match channels: EDOMAIN_EDMAC_1_* to EDOMAIN_EDMAC_7_*
 * where SubChipID is N-1 from above.
 */
extern void PwrMng_WakeSubChips(const uint32_t *list);
extern void PwrMng_SuspendSubChips(const uint32_t *list);

/*
 * A few unavoidable stubs for now. SetupEDMAC does the boomer stuff that 
 * we don't understand.
 * Note: Named MemToMemE1 as they come from `Engine::MemoryToMemoryEsub1.c`
 *       and use EDOMAIN_EDMAC_1_* (thus SubChipID 0) channels.
 *       There's a similar set of functions via `Engine::MemoryToMemoryEsub5.c`
 *       which run on EDOMAIN_EDMAC_5_* (thus SubChipID 4). I had no success
 *       trying those - transfer never happened.
 */
extern void MemToMemE1_SetupEDMAC(uint32_t *channels);
extern void MemToMemE1_RegisterCBR(void *cbr, void *arg);
extern void MemToMemE1_ResetCBR();
extern void MemToMemE1_ClearEdmacCBR();

/*
 * Direct use of EDMAC stubs.
 * What's different from D7 is that ConnectReadEDmac_maybe takes only one arg:
 * read emdac channel. Not a single stub takes "device id" as argument, and 
 * there's no ConnectWriteEDmac equv.
 * Theory for now is: connections are either set up via some of edmac config
 * structures, or Boomer (BoomerVdKick, BoomerSelect...) is responsible for that.
 */
extern void edmac_set_address(uint32_t channel, void *addr);
extern void edmac_set_size(uint32_t channel, struct edmac_info *edmac_info);
extern void edmac_set_transfer_mode(uint32_t channel, uint32_t mode);
extern void StartEDmac_maybe(uint32_t channel);
extern void ConnectReadEDmac_maybe(uint32_t channel);

/*
 * Event flags. Paths use those a lot.
 * We use this to distinguish between a successfull failed data copy
 */
extern uint32_t CreateEventFlag_strictly(const char *name);
extern uint32_t SetEventFlag(uint event_id, uint flag);
extern uint32_t WaitForAnyEventFlag(uint32_t event_id, uint32_t flag, uint32_t timeout);
extern uint32_t ClearEventFlag(uint32_t event_id, uint32_t flag);
extern uint32_t DeleteEventFlag(uint32_t event_id);

/*
 * Some setup for mem2mem EDMAC copy.
 * Values stolen from `EFsVcopy` which seems to be the only user of
 * `Engine::MemoryToMemoryEsub1.c` methods.
 */
const uint32_t mem2mem_RD_CH = 46;
const uint32_t mem2mem_WR_CH = 13;
const uint32_t mem2mem_devices[2] = {0, 7}; 
const uint32_t mem2mem_resources[2] = {0x100AD, 0x100BB};
const uint32_t mem2mem_mode = 0x0; // 1 - 32bit, 2 - 64bit, 3 - 128bit
const uint32_t mem2mem_wait_ms = 10;

struct LockEntry * mem2mem_lock;
uint32_t mem2mem_done;
uint32_t mem2mem_flag;

static void mem2mem_copy_comp_CBR(void * arg)
{
    DryosDebugMsg(0, 15, "CopyCompCBR: %d", arg);
    mem2mem_done = 1;
    SetEventFlag(mem2mem_flag, 1);
}

uint32_t mem2mem_emdac_copy_d8(void * src, void * dst, struct edmac_info * src_info, struct edmac_info * dst_info)
{
    /**
    * "InitMem2MemPath" stage
    */
    DryosDebugMsg(0, 15, "InitMem2MemPath");
    mem2mem_done = 0;
    mem2mem_flag = CreateEventFlag_strictly("Mem2MemD8Copy");
    mem2mem_lock = CreateResLockEntry(mem2mem_resources, sizeof(mem2mem_resources));
    uint32_t err = LockEngineResources(mem2mem_lock);
    if(err > 0)
    {
      DryosDebugMsg(0, 15, "LockEngineResources failed: %d", err);
      return 1;
    }

    PwrMng_WakeSubChips(mem2mem_devices);
    
    uint32_t channels[2] = {mem2mem_RD_CH, mem2mem_WR_CH}; //1st read, 2nd write
    MemToMemE1_SetupEDMAC(channels);
    MemToMemE1_RegisterCBR(mem2mem_copy_comp_CBR, NULL);

    /**
    * "StartMem2MemPath" stage
    */
    DryosDebugMsg(0, 15, "StartMem2MemPath");

    // equiv to MemToMemE1_set_address(&buf0);
    edmac_set_address(mem2mem_WR_CH, dst);
    edmac_set_address(mem2mem_RD_CH, src);

    // equiv to MemToMemE1_set_size_and_flags(...);
    edmac_set_size(mem2mem_WR_CH, dst_info);
    edmac_set_size(mem2mem_RD_CH, src_info);
    edmac_set_transfer_mode(mem2mem_WR_CH, mem2mem_mode);
    edmac_set_transfer_mode(mem2mem_RD_CH, mem2mem_mode);

    // equiv to MemToMemE1_copy_start();
    StartEDmac_maybe(mem2mem_WR_CH);
    StartEDmac_maybe(mem2mem_RD_CH);
    ConnectReadEDmac_maybe(mem2mem_RD_CH);

    DryosDebugMsg(0, 15, "WaitForData");
    // Wait for transfer to end, or timeout
    WaitForAnyEventFlag(mem2mem_flag, 1, mem2mem_wait_ms);
    ClearEventFlag(mem2mem_flag, 1);
    
    /**
    * "TermMem2MemPath" stage
    */
    DryosDebugMsg(0, 15, "TermMem2MemPath");
    MemToMemE1_ResetCBR();
    MemToMemE1_ClearEdmacCBR();
    PwrMng_SuspendSubChips(mem2mem_devices);
    UnLockEngineResources(mem2mem_lock);
    DeleteEventFlag(mem2mem_flag);

    return mem2mem_done ? 0 : 1;
}

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
