/** \file
 * Function overrides needed for R 1.8.0
 */
/*
 * Copyright (C) 2021 Magic Lantern Team
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

#include <dryos.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <consts.h>
#include <lens.h>
#include <edmac.h>


/** MemoryManager own memory pool experiment **/
static int* pMemoryMgr;

extern int uart_printf(const char * fmt, ...);

/* MemoryManager initialization stub */
extern void* MMGR_InitializeRegion(void* region_start, int region_size);
extern void* MMGR_DEFAULT_POOL;
extern void* MMGR_REGION_START;
extern void* MMGR_REGION_END;

/* Internal variants of stubs that take MemoryManager struct */
extern void* _AllocateMemory_impl(void*, size_t);
extern void  _FreeMemory_impl(void*, void*);
extern int   _GetMemoryInformation_impl(void*, int*, int*);
extern int   _GetSizeOfMaxRegion_impl(void*,int*);

/* Wrapper functions to be exposed instead of regular stubs. */
void* _AllocateMemory(size_t size)
{
    return _AllocateMemory_impl(pMemoryMgr, size);
}

void _FreeMemory(void* ptr)
{
    _FreeMemory_impl(pMemoryMgr, ptr);
}

int GetMemoryInformation(int* total, int* free)
{
    return _GetMemoryInformation_impl(pMemoryMgr, total, free);
}

int GetSizeOfMaxRegion(int* max_region)
{
    return _GetSizeOfMaxRegion_impl(pMemoryMgr, max_region);
}

/*
 * Our memory pool needs to be initialized before _mem_init() in ML init task.
 * I guess it would be better to hook into _mem_init(), but I wanted ability to
 * run code in post_init_task from platform dir anyway.
 */
void platform_post_init()
{
    // set default AllocateMemory pool as fallback - in case of init failure
    // it will behave as "stock" MagicLantern code.
    pMemoryMgr = MMGR_DEFAULT_POOL;

    // Disable for now, region is uncached - causes perf problems in LV
    return;
    uint32_t MMGR_REGION_SIZE = (uint32_t)&MMGR_REGION_END - (uint32_t)&MMGR_REGION_START + 1;
    void* result = MMGR_InitializeRegion(&MMGR_REGION_START, MMGR_REGION_SIZE);
    if( result == MMGR_REGION_START )
    {
        pMemoryMgr = MMGR_REGION_START;
        uart_printf("MMGR Region: Start 0x%08x size 0x%08x\n ", &MMGR_REGION_START, MMGR_REGION_SIZE);
        uart_printf("pMemoryMgr 0x%08x\n", pMemoryMgr);
        return;
    }
    uart_printf("MMGR_InitializeRegion failed! pMemoryMgr fall back to 0x%08x\n", pMemoryMgr);
}

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

const uint32_t mem2mem_devices[2] = {0, 7};
const uint32_t mem2mem_resources[2] = {0x100AD, 0x100BB};
#define MEM2MEM_RD_CH 46
#define MEM2MEM_WR_CH 13
#define MEM2MEM_MODE 0x0 // 1 - 32bit, 2 - 64bit, 3 - 128bit
#define MEM2MEM_WAIT_MS 50

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
    //DryosDebugMsg(0, 15, "InitMem2MemPath");
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

    uint32_t channels[2] = {MEM2MEM_RD_CH, MEM2MEM_WR_CH}; //1st read, 2nd write
    MemToMemE1_SetupEDMAC(channels);
    MemToMemE1_RegisterCBR(mem2mem_copy_comp_CBR, NULL);

    /**
     * "StartMem2MemPath" stage
     */
    //DryosDebugMsg(0, 15, "StartMem2MemPath");

    // equiv to MemToMemE1_set_address(&buf0);
    edmac_set_address(MEM2MEM_WR_CH, dst);
    edmac_set_address(MEM2MEM_RD_CH, src);

    // equiv to MemToMemE1_set_size_and_flags(...);
    edmac_set_size(MEM2MEM_WR_CH, dst_info);
    edmac_set_size(MEM2MEM_RD_CH, src_info);
    edmac_set_transfer_mode(MEM2MEM_WR_CH, MEM2MEM_MODE);
    edmac_set_transfer_mode(MEM2MEM_RD_CH, MEM2MEM_MODE);

    // equiv to MemToMemE1_copy_start();
    StartEDmac_maybe(MEM2MEM_WR_CH);
    StartEDmac_maybe(MEM2MEM_RD_CH);
    ConnectReadEDmac_maybe(MEM2MEM_RD_CH);

    //DryosDebugMsg(0, 15, "WaitForData");
    // Wait for transfer to end, or timeout
    WaitForAnyEventFlag(mem2mem_flag, 1, MEM2MEM_WAIT_MS);
    ClearEventFlag(mem2mem_flag, 1);

    /**
     * "TermMem2MemPath" stage
     */
    //DryosDebugMsg(0, 15, "TermMem2MemPath");
    MemToMemE1_ResetCBR();
    MemToMemE1_ClearEdmacCBR();
    PwrMng_SuspendSubChips(mem2mem_devices);
    UnLockEngineResources(mem2mem_lock);
    DeleteEventFlag(mem2mem_flag);

    return mem2mem_done ? 0 : 1;
}



/*
 * Partition tables stuff. Got inlined in new generations, but this is a pretty standard one.
 * Struct copied from bootflags.c as there's no header to include right now.
 */


struct chs_entry
{
    uint8_t head;
    uint8_t sector; //sector + cyl_msb
    uint8_t cyl_lsb;
}__attribute__((packed));

struct partition
{
    uint8_t  state;
    struct   chs_entry start;
    uint8_t  type;
    struct   chs_entry end;
    uint32_t start_sector;
    uint32_t size;
}__attribute__((aligned,packed));

struct partition_table
{
    uint8_t  state; // 0x80 = bootable
    uint8_t  start_head;
    uint16_t start_cylinder_sector;
    uint8_t  type;
    uint8_t  end_head;
    uint16_t end_cylinder_sector;
    uint32_t sectors_before_partition;
    uint32_t sectors_in_partition;
}__attribute__((packed));

void fsuDecodePartitionTable(void * partIn, struct partition_table * pTable){
    struct partition * part = (struct partition *) partIn;
    pTable->state      = part->state;
    pTable->type       = part->type;
    pTable->start_head = part->start.head;
    pTable->end_head   = part->end.head;
    pTable->sectors_before_partition = part->start_sector;
    pTable->sectors_in_partition     = part->size;

    //tricky bits - TBD
    pTable->start_cylinder_sector = 0;
    pTable->end_cylinder_sector   = 0;

    uart_printf("Bootflag: %02x\n", pTable->state);
    uart_printf("Type: %02x\n", pTable->type);
    uart_printf("Head start: %02x end %02x\n", pTable->start_head, pTable->end_head);
    uart_printf("Sector start: %08x size %08x\n", pTable->sectors_before_partition, pTable->sectors_in_partition);
    uart_printf("CS: Start %04x End %04x\n", pTable->start_cylinder_sector, pTable->end_cylinder_sector);
}

/** GUI **/
//see comments in stub.S
void gui_init_end(void){ }

//Not sure if all args are uint.
extern void gui_enqueue_message(uint32_t, uint32_t, uint32_t, uint32_t);
void GUI_Control(int bgmt_code, int obj, int arg, int unknown){
    gui_enqueue_message(0, bgmt_code, obj, arg);
}

void LoadCalendarFromRTC(struct tm *tm)
{
    _LoadCalendarFromRTC(tm, 0, 0, 16);
}

int get_task_info_by_id(int unknown_flag, int task_id, void *task_attr)
{
    // task_id is something like two u16s concatenated.  The flag argument,
    // present on D45 but not on D678 allows controlling if the task info request
    // uses the whole thing, or only the low half.
    //
    // ML calls with this set to 1, meaning task_id is used as is,
    // if 0, the high half is masked out first.
    //
    // D678 doesn't have the 1 option, we use the low half as index
    // to find the full value.
    struct task *task = first_task + (task_id & 0xffff);
    return _get_task_info_by_id(task->taskId, task_attr);
}

/** File I/O **/

/**
 * _FIO_GetFileSize returns now 64bit size in form of struct.
 * This probably should be integrated into fio-ml for CONFIG_DIGIC_VIII
 */
extern int _FIO_GetFileSize64(const char *, void *);
int _FIO_GetFileSize(const char * filename, uint32_t * size){
    uint32_t size64[2];
    int code = _FIO_GetFileSize64(filename, &size64);
    *size = size64[0]; //return "lower" part
    return code;
}

// No shamem on D8, reads are done directly
uint32_t shamem_read(uint32_t addr)
{
    return *(uintptr_t*)addr;
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

    /* create a memory suite from a already existing (continuous) memory block with given size. */
    // SJE: no idea what "memory suite" is supposed to mean here
    uint32_t src_adjusted = ((uint32_t)src & 0x1FFFFFFF) + src_x + src_y * src_width;
    uint32_t dst_adjusted = ((uint32_t)dst & 0x1FFFFFFF) + dst_x + dst_y * dst_width;

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
void edmac_memcpy_res_lock()
{
}

void edmac_memcpy_res_unlock()
{
}

void edmac_copy_rectangle_adv_cleanup()
{
}

uint32_t edmac_read_chan = 0x6; // dummy, don't use
uint32_t edmac_write_chan = 0x6; // dummy, don't use

/** WRONG: temporary overrides to get CONFIG_HELLO_WORLD working **/

void SetEDmac(unsigned int channel, void *address, struct edmac_info *ptr, int flags)
{
    return;
}

void ConnectWriteEDmac(unsigned int channel, unsigned int where)
{
    return;
}

void ConnectReadEDmac(unsigned int channel, unsigned int where)
{
    return;
}

void StartEDmac(unsigned int channel, int flags)
{
    return;
}

void AbortEDmac(unsigned int channel)
{
    return;
}

void RegisterEDmacCompleteCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacCompleteCBR(int channel)
{
    return;
}

void RegisterEDmacAbortCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacAbortCBR(int channel)
{
    return;
}

void RegisterEDmacPopCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacPopCBR(int channel)
{
    return;
}

void _EngDrvOut(uint32_t reg, uint32_t value)
{
    return;
}

void _engio_write(uint32_t* reg_list)
{
    return;
}
