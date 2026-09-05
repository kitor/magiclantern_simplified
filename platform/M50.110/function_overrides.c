/** \file
 * Function overrides needed for M50 1.1.0
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
#include <raw.h>
#include <picstyle.h>

#include "internals.h"  /* for CONFIG_RAW_LIVEVIEW and other platform defines */
#include "cpu.h"
#include "dryos_rpc.h"

/*
 * On DIGIC 7/8 class cameras, some MMIO/shared memory reads are safer when executed
 * on CPU1 via request_RPC().
 */
struct shamem_read_req
{
   uint32_t addr;
   uint32_t value;
   volatile uint32_t done;
};

static void shamem_read_cpu1(void *arg)
{
   struct shamem_read_req *r = (struct shamem_read_req *) arg;
   r->value = *(volatile uint32_t *) r->addr;
   r->done = 1;
   asm("dsb 0xf");
}

#include <string.h>

/* for size_t */
#include <stddef.h>

/** GUI **/

//Not sure if all args are uint.
extern void gui_enqueue_message(uint32_t, uint32_t, uint32_t, uint32_t);
void GUI_Control(int bgmt_code, int obj, int arg, int unknown){
   gui_enqueue_message(0, bgmt_code, obj, arg);
}

// fake WINSYS_BMP_DIRTY_BIT_NEG
int winsys_bmp_dirty_bit_neg = 0;

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

uint32_t shamem_read(uint32_t addr)
{
   /**
    * M50/DIGIC 8: Some MMIO ranges hang the CPU when read.
    * Strategy: allow RPC reads for safe ranges, return 0 for dangerous ones.
    *
    * SAFE to read via CPU1 RPC:
    *   0x00000000-0x1FFFFFFF  cacheable RAM
    *   0x40000000-0x5FFFFFFF  uncacheable RAM
    *   0xE0000000-0xFFFFFFFF  ROM (decrypted in RAM on DIGIC 8)
    *
    * DANGEROUS (hang CPU):
    *   0xC0F00000-0xC0FFFFFF  DIGIC image processing regs
    *   0xD0400000-0xD04FFFFF  EDMAC MMIO
    *   0xD0000000-0xDFFFFFFF  other MMIO
    *
    * edmac.c, lv-img-engio.c, raw.c all call shamem_read with 0xC0F/0xD04
    * addresses during normal LV — those must return 0 to avoid hangs.
    */
   uint32_t top4 = addr >> 28;

   /* RAM: safe to read directly from CPU0, no RPC needed */
   if (top4 == 0x0 || top4 == 0x1 || top4 == 0x4 || top4 == 0x5)
   {
      return *(volatile uint32_t *)(addr & ~3);
   }

   /* ROM: read via CPU1 RPC with timeout */
   if (top4 >= 0xE)
   {
      if (get_cpu_id() == 1)
      {
         return *(volatile uint32_t *)(addr & ~3);
      }

      struct shamem_read_req req = { .addr = (addr & ~3), .value = 0, .done = 0 };
      struct RPC_args rpc = { .RPC_func = shamem_read_cpu1, .RPC_arg = &req };

      if (request_RPC(&rpc) < 0)
      {
         return 0;
      }

      /* Timeout: ~50ms at DIGIC 8 clock speeds */
      int timeout = 5000000;
      while (!req.done && --timeout > 0)
      {
         asm("dsb 0xf");
      }
      return req.done ? req.value : 0;
   }

   /* MMIO (0xC0xxxxxx, 0xD0xxxxxx, etc): unsafe, return 0 */
   return 0;
}

void _engio_write(uint32_t* reg_list)
{
   return;
}

unsigned int UnLockEngineResources(struct LockEntry *lockEntry)
{
   return 0;
}

/**
 * Minimal EDMAC / edmac-memcpy shims for early ports.
 *
 * Rationale:
 * - raw-video modules (e.g. mlv_lite, mlv_rec, raw_vid) depend on the
 *   edmac-memcpy API symbols.
 * - M50 port does not have working EDMAC control yet; provide a slow but
 *   functional CPU fallback so modules can load and basic workflows can be
 *   tested.
 *
 * Note:
 * - Widths / offsets are in BYTES (see src/edmac-memcpy.h).
 * - Callbacks are invoked synchronously; this is sufficient for clearing
 *   module state machines that wait for the EDMAC completion CBR.
 */

/* Dummy channel globals (required by some modules for UI/debug output). */
uint32_t edmac_read_chan = 0;
uint32_t edmac_write_chan = 0;

/* EDMAC passthrough connection index used by dev EDMAC tests and edmac-memcpy. */
uint32_t dmaConnection = 6;

/*
 * Some modules rely on helper functions that may be compiled out by config
 * on early ports. Provide safe fallbacks so modules can at least link/load.
 */

/*
 * picstyle.h only declares these getters for core when FEATURE_PICSTYLE is enabled,
 * or for modules (when MODULE is defined). We still want to export them for modules,
 * so declare them explicitly here.
 */
extern const char * picstyle_get_current_name(void);
extern int picstyle_get_current_sharpness(void);
extern int picstyle_get_current_contrast(void);
extern int picstyle_get_current_saturation(void);
extern int picstyle_get_current_color_tone(void);

static uint32_t isqrt_u32(uint32_t n)
{
   /* integer sqrt (floor) */
   uint32_t res = 0;
   uint32_t bit = 1u << 30;
   while (bit > n) bit >>= 2;
   while (bit)
   {
      if (n >= res + bit)
      {
         n -= res + bit;
         res = (res >> 1) + bit;
      }
      else
      {
         res >>= 1;
      }
      bit >>= 2;
   }
   return res;
}

uint32_t edmac_find_divider(size_t length, size_t transfer_size)
{
   if (length == 0)
   {
      return 0;
   }

   /* Avoid hard dependency on working EDMAC; caller may pass transfer_size. */
   if (transfer_size == 0)
   {
      /* default to 16 bytes per transfer (common on newer DIGIC) */
      transfer_size = 16;
   }

   if (transfer_size && (length % transfer_size))
   {
      return 0;
   }

   uint32_t max_width = isqrt_u32((uint32_t) length);
   for (uint32_t width = max_width; width > 0; width--)
   {
      if (length % width == 0)
      {
         return width;
      }
   }
   return 0;
}

#ifndef CONFIG_STATE_OBJECT_HOOKS
int wait_lv_frames(int num_frames)
{
   /* No vsync hook on early M50; approximate with sleeps. */
   if (num_frames <= 0)
   {
      return 0;
   }

   extern int lv;
   for (int i = 0; i < num_frames; i++)
   {
      if (!lv)
      {
         return 0;
      }
      /* ~30fps-ish */
      msleep(35);
   }

   return 1;
}
#endif

/*
 * RAW LV / picture style helpers.
 *
 * On M50 early ports, these are often compiled out in core (e.g. no CONFIG_RAW_LIVEVIEW,
 * FEATURE_PICSTYLE disabled), yet raw-video modules still reference them.
 * Provide safe stubs so modules can at least load.
 */
#ifndef CONFIG_RAW_LIVEVIEW
/* Compile-time check: if this block is active, CONFIG_RAW_LIVEVIEW is NOT defined.
 * This would cause the stubs to win at link time, resulting in req=0 forever.
 * If you see this error, CONFIG_RAW_LIVEVIEW is missing from internals.h or not visible here. */
#error "CONFIG_RAW_LIVEVIEW not defined in function_overrides.c - check includes"
void raw_lv_request(void) {}
void raw_lv_release(void) {}
void raw_lv_redirect_edmac(void* ptr) { (void) ptr; }
void raw_lv_request_bpp(int bpp) { (void) bpp; }
void raw_lv_request_digital_gain(int gain) { (void) gain; }
int raw_lv_settings_still_valid(void) { return 0; }

int focus_box_get_raw_crop_offset(int* delta_x, int* delta_y)
{
   if (delta_x) *delta_x = 0;
   if (delta_y) *delta_y = 0;
   return 0;
}
#endif

const char * picstyle_get_current_name(void) { return ""; }
int picstyle_get_current_sharpness(void) { return 0; }
int picstyle_get_current_contrast(void) { return 0; }
int picstyle_get_current_saturation(void) { return 0; }
int picstyle_get_current_color_tone(void) { return 0; }

/* SRM memory suite fallbacks (M50 early ports often disable SRM). */
struct memSuite;
struct memSuite * srm_malloc_suite(int num_requested_buffers)
{
   (void) num_requested_buffers;
   return NULL;
}

void srm_free_suite(struct memSuite * suite)
{
   (void) suite;
}

/* LiveView error dialog handler stub referenced by mlv_lite. */
void ErrCardForLVApp_handler(void) {}

void edmac_memcpy_res_lock(void)
{
   /* no-op (no engine resource locking implemented yet) */
}

void edmac_memcpy_res_unlock(void)
{
   /* no-op */
}

void edmac_copy_rectangle_adv_cleanup(void)
{
   /* no-op for the CPU fallback */
}

/*
 * =====================================================================
 * EDMAC DMA copy via ELD Edmac Low Driver (DIGIC 8)
 * =====================================================================
 *
 * M50 EDMAC MMIO register offsets (from struct edmac_mmio):
 *   +0x08 : command  (0=idle, 2/3/0x11=reset, 0x12=stop)
 *   +0x20 : DMA enable flag a
 *   +0x24 : DMA enable flag b
 *   +0x3C : cbr_registered
 *   +0x48 : ys_xs    (packed 16|16)
 *   +0x4C : ya_xa    (packed 16|16)
 *   +0x50 : yb_xb    (packed 16|16)
 *   +0x54 : yn_xn    (packed 16|16)
 *   +0x58 : off1s
 *   +0x5C : off2s
 *   +0x60 : off1a
 *   +0x64 : off2a
 *   +0x68 : off1b
 *   +0x6C : off2b
 *   +0x70 : off3
 *   +0xA0 : ram_addr     (primary address)
 *   +0xAC : ram_addr2    (secondary address, possibly for mem-to-mem)
 *   +0xB4 : status/ack   (1=after stop, 2=after reset)
 *   +0xC0 : transfer_mode (0-3)
 *   +0xDC : start trigger (write 1 to start)
 *
 * Channel table in ROM at 0xE0DD7BA0:
 *   Each entry 8 bytes: {uint32_t mmio_base, uint32_t flags}
 *   76 channels total (0x00-0x4B)
 *
 * ELD Edmac functions (stubs in stubs.S):
 *   eld_edmac_reset(ch)             - reset EDMAC channel
 *   eld_edmac_clear_enable(ch)      - clear DMA enable flags
 *   eld_edmac_set_addr(ch, addr)    - set ram_addr [+0xA0]
 *   eld_edmac_set_addr2(ch, addr)   - set second addr [+0xAC]
 *   eld_edmac_set_geom(ch, info*)   - write edmac_info geometry
 *   eld_edmac_set_mode(ch, mode)    - set transfer_mode [+0xC0]
 *   eld_edmac_start(ch)             - start (flag check + trigger)
 *   eld_edmac_stop(ch)              - stop channel
 */

/* ELD Edmac function declarations (addresses in stubs.S) */
extern void eld_edmac_init(uint32_t channel);
extern void eld_edmac_reset(uint32_t channel);
extern void eld_edmac_stop(uint32_t channel);
extern void eld_edmac_config(uint32_t channel, void *config, uint32_t mode);
extern void eld_edmac_clear_enable(uint32_t channel);
extern void eld_edmac_set_enable(uint32_t channel);
extern void eld_edmac_set_addr(uint32_t channel, uint32_t addr);
extern void eld_edmac_set_addr2(uint32_t channel, uint32_t addr);
extern void eld_edmac_set_geom(uint32_t channel, struct edmac_info *info);
extern void eld_edmac_set_mode(uint32_t channel, uint32_t mode);
extern void eld_edmac_start(uint32_t channel);
extern void eld_edmac_trigger(uint32_t channel);
extern void eld_edmac_irq_enable(uint32_t channel);

/* DarkCurCor mem-to-mem Layer 2 functions (proven working in v5.1 test) */
extern void m2m_InitMem2MemModule(void *ch_pair);
extern void m2m_store_struct(void *cbr, uint32_t arg);
extern void m2m_reinit_struct(void);
extern void m2m_cleanup_channels(void);
extern void m2m_set_addrs(uint32_t *addr_pair);
extern void m2m_set_geom_mode(uint32_t *geom_desc);
extern void m2m_start_connect(void);
extern uint32_t m2m_resource_lock(void *res_entry, uint32_t flags);
extern void m2m_resource_unlock(uint32_t handle);
extern void m2m_pwr_wake(void);
extern void m2m_pwr_sleep(void);

/* EfmErsc resource locking declarations */
extern void *EfmErscCreateLockEntry(uint32_t *resIds, uint32_t resIdCount, uint32_t flags);
extern uint32_t EfmErscLockResources(void *lockEntry);
extern uint32_t EfmErscUnLockResources(void);
extern uint32_t EfmErscDeleteLockEntry(void);

/* DarkCurCor mem-to-mem channel pair (proven in v5.1: 64/64 words)
 *   Write (source):      ch 0x3D, MMIO 0xD0487600
 *   Read  (destination): ch 0x18, MMIO 0xD0487100
 * ROM data: ch_pair at 0xE0F72640, res_entry at 0xE0F72638
 */
#define M2M_WRITE_CH    0x3D
#define M2M_READ_CH     0x18
#define M2M_WR_MMIO     0xD0487600
#define M2M_RD_MMIO     0xD0487100
#define DARKCURCOR_RES_ENTRY  ((void *)0xE0F72638)
#define DARKCURCOR_CH_PAIR    ((void *)0xE0F72640)
#define M2M_NOP_CBR           ((void *)0xE084DF49)

/* Set to 1 to enable EDMAC DMA copy, 0 for CPU fallback */
static int edmac_dma_enabled = 1;

static void edmac_copy_rectangle_cpu(
   uint8_t *dst, uint8_t *src,
   int src_width, int dst_width,
   int w, int h)
{
   for (int y = 0; y < h; y++)
   {
      memcpy(dst, src, (size_t) w);
      src += src_width;
      dst += dst_width;
   }
}

void* edmac_copy_rectangle_cbr_start(void* dst, void* src,
                            int src_width, int src_x, int src_y,
                            int dst_width, int dst_x, int dst_y,
                            int w, int h,
                            void (*cbr_r)(void*), void (*cbr_w)(void*), void *cbr_ctx)
{
   if (!dst || !src || w <= 0 || h <= 0 || src_width <= 0 || dst_width <= 0)
   {
      return NULL;
   }

   uint8_t* s = (uint8_t*) src + src_y * src_width + src_x;
   uint8_t* d = (uint8_t*) dst + dst_y * dst_width + dst_x;

   if (edmac_dma_enabled)
   {
      /* Flush cache before DMA if source is in cacheable memory */
      if ((uint32_t)s < 0x40000000)
         sync_caches();

      /* Source geometry (write channel reads FROM source memory) */
      struct edmac_info src_geom = { 0 };
      src_geom.xb = w;                   /* bytes per line */
      src_geom.yb = h - 1;              /* line count minus 1 */
      src_geom.off1b = src_width - w;    /* skip at end of each source row */

      /* Destination geometry (read channel writes TO dest memory) */
      struct edmac_info dst_geom = { 0 };
      dst_geom.xb = w;
      dst_geom.yb = h - 1;
      dst_geom.off1b = dst_width - w;

      /* Layer 2 descriptors:
       *   addr_pair[0] = write_ch source, addr_pair[1] = read_ch dest
       *   geom_desc = {write_geom*, read_geom*, mode} */
      uint32_t addr_pair[2] = { (uint32_t)s, (uint32_t)d };
      uint32_t geom_desc[3] = {
         (uint32_t)&src_geom,    /* write channel geometry (source) */
         (uint32_t)&dst_geom,    /* read channel geometry (dest) */
         1                       /* mode = 1 (standard DMA) */
      };

      /* Power up DMA engine and lock resources */
      uint32_t lock = m2m_resource_lock(DARKCURCOR_RES_ENTRY, 2);
      m2m_pwr_wake();

      /* Initialize channels, crossbar, ISRs via proven Layer 2 API */
      m2m_InitMem2MemModule(DARKCURCOR_CH_PAIR);
      m2m_store_struct(M2M_NOP_CBR, 0);
      m2m_set_addrs(addr_pair);
      m2m_set_geom_mode(geom_desc);

      /* Enable DMA on both channels + IRQ on read (dest) channel */
      eld_edmac_set_enable(M2M_READ_CH);
      eld_edmac_set_enable(M2M_WRITE_CH);
      eld_edmac_irq_enable(M2M_READ_CH);

      /* Data barrier, then trigger transfer */
      asm volatile ("dsb sy" ::: "memory");
      m2m_start_connect();

      /* Poll write channel CONN_ST for completion (transitions 2→0) */
      volatile uint32_t *wr_conn = (volatile uint32_t *)(M2M_WR_MMIO + 0xB4);
      int timeout = 1000000;
      while (*wr_conn != 0 && --timeout > 0) { }

      /* Cleanup: reset state, power down */
      m2m_reinit_struct();
      m2m_cleanup_channels();
      m2m_pwr_sleep();
      m2m_resource_unlock(lock);
   }
   else
   {
      /* CPU fallback: simple row-by-row memcpy */
      edmac_copy_rectangle_cpu(d, s, src_width, dst_width, w, h);
   }

   if (cbr_r) cbr_r(cbr_ctx);
   if (cbr_w) cbr_w(cbr_ctx);
   return dst;
}

struct LockEntry *CreateResLockEntry(uint32_t *resIds, uint32_t resIdCount)
{
   (void) resIds;
   (void) resIdCount;

   /* not implemented yet for M50; return NULL (modules using the CPU fallback don't rely on it) */
   return NULL;
}

unsigned int LockEngineResources(struct LockEntry *lockEntry)
{
   (void) lockEntry;
   return 0;
}

/*
 * Prevent linker GC from dropping the shim symbols.
 * These functions are primarily used by modules, so they may appear unused
 * from the core binary's perspective.
 */
static void keep_raw_video_shim_symbols(void* unused)
{
   (void) unused;
   void * volatile keep[] = {
      (void*) edmac_memcpy_res_lock,
      (void*) edmac_memcpy_res_unlock,
      (void*) edmac_copy_rectangle_adv_cleanup,
      (void*) edmac_copy_rectangle_cbr_start,
      (void*) edmac_find_divider,
      (void*) edmac_get_base,
      (void*) edmac_get_length,
      (void*) edmac_channel_to_index,
      (void*) CreateResLockEntry,
      (void*) LockEngineResources,
      (void*) UnLockEngineResources,
      (void*) &edmac_read_chan,
      (void*) &edmac_write_chan,
      (void*) &dmaConnection,
      (void*) wait_lv_frames,
      (void*) raw_lv_request,
      (void*) raw_lv_release,
      (void*) raw_lv_redirect_edmac,
      (void*) raw_lv_request_bpp,
      (void*) raw_lv_request_digital_gain,
      (void*) raw_lv_settings_still_valid,
      (void*) focus_box_get_raw_crop_offset,
      (void*) srm_malloc_suite,
      (void*) srm_free_suite,
      (void*) picstyle_get_current_name,
      (void*) picstyle_get_current_sharpness,
      (void*) picstyle_get_current_contrast,
      (void*) picstyle_get_current_saturation,
      (void*) picstyle_get_current_color_tone,
      (void*) ErrCardForLVApp_handler,
      (void*) raw_lv_get_fail_reason,
   };
   (void) keep;
}

INIT_FUNC("keep_raw_video_shims", keep_raw_video_shim_symbols);
