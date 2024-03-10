/** \file
 * edmac-memcpy.c reimplementation for Digic 8 in general.
 */
/*
 * Copyright (C) 2024 Magic Lantern Team
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

// mimic edmac-memcpy.c
#ifndef CONFIG_EDMAC_MEMCPY_D8
#include <dryos.h>
#include <edmac.h>
#include <arm-mcr.h>
/*
 * EDMAC mem2mem copy
 */

/*
 * On D8 those take a list of subchips (0-6), terminated with 7.
 * Those seem to match channels: EDOMAIN_EDMAC_1_* to EDOMAIN_EDMAC_7_*
 * where SubChipID is N-1 from above.
 * If you don't wake SubChip before any edmac mmio r/w attempt, camera will
 * hard lock.
 */
extern void PwrMng_WakeSubChips(const uint32_t *list);
extern void PwrMng_SuspendSubChips(const uint32_t *list);


/*
 * Direct use of EDMAC stubs.
 * What's different from D7 is that ConnectReadEDmac_maybe takes only one arg:
 * read emdac channel. Not a single stub takes "device id" as argument, and
 * there's no ConnectWriteEDmac equv.
 * Theory for now is: connections are either set up via some of edmac config
 * structures, or Boomer (BoomerVdKick, BoomerSelect...) is responsible for that.
 */
extern void edmac_reset_channel(uint32_t channel);
extern void edmac_reset_boomer_vdkick(uint32_t channel);
extern void edmac_reset_packunpack_mode(uint32_t channel);
extern void edmac_set_address(uint32_t channel, void *addr);
extern void edmac_set_size(uint32_t channel, struct edmac_info *edmac_info);
extern void edmac_set_transfer_mode(uint32_t channel, uint32_t mode);
extern void edmac_select_boomer(uint32_t channel, uint32_t flags);
extern void StartEDmac_maybe(uint32_t channel);            // call twice for each channel
extern void ConnectReadEDmac_maybe(uint32_t wr_channel);   // call only for write channel!
extern void edmac_stop_boomer_maybe(uint32_t channel);


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
 * Values stolen from R180 `EFsVcopy` which seems to be the only user of
 * `Engine::MemoryToMemoryEsub1.c` methods.
 * DNE on SX740, but config still works.
 */
const uint32_t mem2mem_devices[2] = {0, 7};
uint32_t mem2mem_resources[2] = {0x100AD, 0x100BB};
#define MEM2MEM_RD_CH 46
#define MEM2MEM_WR_CH 13
#define MEM2MEM_BOOMER_SELECTOR 0x373f0126
#define MEM2MEM_MODE 0x0 // 1 - 32bit, 2 - 64bit, 3 - 128bit
#define MEM2MEM_WAIT_MS 50

struct LockEntry * mem2mem_lock;
uint32_t mem2mem_event;
uint32_t mem2mem_status;

static void mem2mem_CBR(uint32_t arg)
{
  uint32_t old_irq = cli();
  mem2mem_status = mem2mem_status | arg;
  sei(old_irq);
  if (mem2mem_status == 3) {
    SetEventFlag(mem2mem_event, 1);
  }
}

uint32_t mem2mem_emdac_copy_d8(void * src, void * dst, struct edmac_info * src_info, struct edmac_info * dst_info)
{

    // reset channels
    edmac_reset_channel(MEM2MEM_RD_CH);
    edmac_reset_channel(MEM2MEM_WR_CH);

    // boomer selector - the great D8 unknown.
    edmac_reset_boomer_vdkick(MEM2MEM_WR_CH);
    edmac_select_boomer(MEM2MEM_WR_CH, MEM2MEM_BOOMER_SELECTOR);

    mem2mem_event = CreateEventFlag_strictly("Mem2MemD8Copy");
    mem2mem_status = 0;
    RegisterEDmacCompleteCBR(MEM2MEM_RD_CH, mem2mem_CBR, 1);
    RegisterEDmacCompleteCBR(MEM2MEM_WR_CH, mem2mem_CBR, 2);

    edmac_reset_packunpack_mode(MEM2MEM_RD_CH);
    edmac_reset_packunpack_mode(MEM2MEM_WR_CH);

    /**
     * "StartMem2MemPath" stage
     */
    edmac_set_address(MEM2MEM_WR_CH, dst);
    edmac_set_address(MEM2MEM_RD_CH, src);

    edmac_set_size(MEM2MEM_WR_CH, dst_info);
    edmac_set_size(MEM2MEM_RD_CH, src_info);
    edmac_set_transfer_mode(MEM2MEM_WR_CH, MEM2MEM_MODE);
    edmac_set_transfer_mode(MEM2MEM_RD_CH, MEM2MEM_MODE);

    StartEDmac_maybe(MEM2MEM_WR_CH);
    StartEDmac_maybe(MEM2MEM_RD_CH);
    ConnectReadEDmac_maybe(MEM2MEM_RD_CH);

    // Wait for transfer to either end or timeout
    WaitForAnyEventFlag(mem2mem_event, 1, MEM2MEM_WAIT_MS);
    ClearEventFlag(mem2mem_event, 1);

    /**
     * "TermMem2MemPath" stage
     */
    UnregisterEDmacCompleteCBR(MEM2MEM_WR_CH);
    UnregisterEDmacCompleteCBR(MEM2MEM_RD_CH);
    edmac_stop_boomer_maybe(MEM2MEM_WR_CH);

    DeleteEventFlag(mem2mem_event);

    return mem2mem_status == 3 ? 0 : 1;
}

void edmac_memcpy_res_lock()
{
    mem2mem_lock = CreateResLockEntry(mem2mem_resources, sizeof(mem2mem_resources)/sizeof(mem2mem_resources[0]));
    LockEngineResources(mem2mem_lock);

    PwrMng_WakeSubChips(mem2mem_devices);
}

void edmac_memcpy_res_unlock()
{
    PwrMng_SuspendSubChips(mem2mem_devices);
    UnLockEngineResources(mem2mem_lock);
}

// reimplement this wonderful function with mem2mem_emdac_copy_d8
void* edmac_copy_rectangle_cbr_start(void *dst, void *src,
                                     int src_width, int src_x, int src_y,
                                     int dst_width, int dst_x, int dst_y,
                                     int w, int h,
                                     void (*cbr_r)(void *), void (*cbr_w)(void *), void *cbr_ctx)
{
    // "src_width" is width of the frame including dark areas, borders etc.
    // "w" is the width of the region to copy out, e.g. if stripping the borders.

    if ((src == NULL) || (dst == NULL))
    {
        //ASSERT(0);
        return NULL;
    }

    // Old code doesn't explain why it checks this, my guess is
    // because DMA transfers don't invalidate CPU cache, since
    // they're outside of the CPU.
    //ASSERT(dst == UNCACHEABLE(dst));

    /* clean the cache before reading from regular (cacheable) memory */
    /* see FIO_WriteFile for more info */
    if (src != UNCACHEABLE(src)) // inverted to make 2GB compatible
    {
        sync_caches();
    }

    struct edmac_info src_region = {
        .off1b = src_width - w,
        .xb = w,
        .yb = h - 1,
    };

    struct edmac_info dst_region = {
        .off1b = 0,
        .xb = w,
        .yb = h - 1,
    };

    mem2mem_emdac_copy_d8(src, dst, &src_region, &dst_region);
    cbr_w(NULL); // clears edmac_active, we have no CBR to do this
    return dst;
}

// dummy stubs to make mlv_lite happy
void edmac_copy_rectangle_adv_cleanup()
{
}

uint32_t edmac_read_chan = 0x6; // dummy, don't use
uint32_t edmac_write_chan = 0x6; // dummy, don't use

#endif
