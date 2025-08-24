/** \file
 * Entry points into the firmware image.
 *
 * These are the functions that we can call from our tasks
 * in the Canon 1.1.1 firmware for the EOS M6 Mark II
 *
 */
/*
 * Copyright (C) 2025 Magic Lantern Team
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


// A big chunk of this file was auto-generated from Ghidra project
// using R.180 as a stubs source

#include <stub.h>

/** Startup **/
THUMB_FN(0xe0040104, cstart)
THUMB_FN(0xe059a5f4, bzero32)
THUMB_FN(0xe028bcfc, create_init_task)
THUMB_FN(0xe0599200, dcache_clean)
THUMB_FN(0xe05992d4, icache_invalidate)
THUMB_FN(0xe0040236, init_task)

/** Tasks **/
DATA_PTR(0x1010, current_interrupt)
DATA_PTR(0x1028, current_task)
THUMB_FN(0xe058599a, msleep)
THUMB_FN(0xe059da7a, task_create)
DATA_PTR( 0x4118, task_max)

/** File I/O **/
THUMB_FN(0xe0579faa, _FIO_CreateFile)
THUMB_FN(0xe0579ef8, _FIO_OpenFile)
THUMB_FN(0xe057a750, FIO_CloseFile)
THUMB_FN(0xe057a382, _FIO_ReadFile)
THUMB_FN(0xe057a534, _FIO_WriteFile)
THUMB_FN(0xe057a476, FIO_SeekSkipFile)
THUMB_FN(0xe057a2ea, _FIO_RemoveFile)
THUMB_FN(0xe057b34e, _FIO_RenameFile)
THUMB_FN(0xe057b7d6, _FIO_CreateDirectory)
THUMB_FN(0xe057bc30, _FIO_FindFirstEx)
THUMB_FN(0xe057bd2c, FIO_FindNextEx)
THUMB_FN(0xe057bdee, FIO_FindClose)
THUMB_FN(0xe057b9ba, FIO_Flush)
THUMB_FN(0xe057a8ea, _FIO_GetFileSize64)

/** Bitmap **/
DATA_PTR(0x78c78, bmp_vram_info)  // via EOS RP.

/** Debug messages **/
THUMB_FN(0xe0599848, DryosDebugMsg)

/** Eventprocs (call by name) **/
THUMB_FN(0xe05830b6, call)

/** GUI timers **/
THUMB_FN(0xe03d8ba2, SetHPTimerNextTick)
THUMB_FN(0xe03d8b32, SetHPTimerAfterNow)
THUMB_FN(0xe0584d14, SetTimerAfter)
THUMB_FN(0xe0584dca, CancelTimer)
THUMB_FN(0xe058c174, _alloc_dma_memory)
THUMB_FN(0xe058c168, _free_dma_memory)
THUMB_FN(0xe0360e94, _malloc)
THUMB_FN(0xe0360ecc, _free)

/** Memory allocation **/
THUMB_FN(0xe058b9b8, _AllocateMemory)                       /* NG AllocateMemory */
THUMB_FN(0xe058bb70, _FreeMemory)                           /* uses the same heap */
THUMB_FN(0xe058b842,  GetMemoryInformation)                 /* called from AllocateMemory */
THUMB_FN(0xe058b816,  GetSizeOfMaxRegion)                   /* same */

/** Memory info **/
THUMB_FN(0xe03653a0, memmap_info)
THUMB_FN(0xe010eb7c, smemShowFix)
THUMB_FN(0xe03926ec, malloc_info)
THUMB_FN(0xe0392784, sysmem_info)

/** Misc **/
THUMB_FN(0xe0587590, uart_printf)
THUMB_FN(0xe0061238, vsnprintf)

/** Startup **/
//DATA_PTR(0x, additional_version) // TODO: Seems to be a part of structure now

/** Tasks **/
DATA_PTR(0x4154, task_dispatch_hook)
THUMB_FN(0xe008faa8, task_trampoline)
THUMB_FN(0xe01b765c, _get_task_info_by_id)

/** Recursive locks **/
THUMB_FN(0xe0598e34, CreateRecursiveLock)
THUMB_FN(0xe0577944, AcquireRecursiveLock)
THUMB_FN(0xe0577a0e, ReleaseRecursiveLock)

/** Semaphores **/
THUMB_FN(0xe0599358, create_named_semaphore)
THUMB_FN(0xe05994ce, give_semaphore)
THUMB_FN(0xe0599454, take_semaphore)
THUMB_FN(0xe0599424, take_semaphore_now)

/** GUI **/
DATA_PTR(0x11960, gui_task_list)
THUMB_FN(0xe0557032, SetGUIRequestMode)
THUMB_FN(0xe01dcb18, gui_main_task)
THUMB_FN(0xe03db75a, gui_massive_event_loop)
THUMB_FN(0xe01dcc4a, gui_enqueue_message)
DATA_PTR(0x5488, gui_main_struct)

/** Dialog API **/
THUMB_FN(0xe0562b32, dialog_redraw)
THUMB_FN(0xe055f3a0, dialog_set_property_str)

/** GUI timers **/
THUMB_FN(0xe069e62e, StartDateTimer)
THUMB_FN(0xe069e6e2, CancelDateTimer)

/** Queue **/
THUMB_FN(0xe057df00, msg_queue_create)
THUMB_FN(0xe057dfc4, msg_queue_receive)
THUMB_FN(0xe057e122, msg_queue_post)
THUMB_FN(0xe057e158, msg_queue_count)

/** Memory **/
THUMB_FN(0xe0663838, AllocateMemoryResource)
THUMB_FN(0xe0663866, AllocateContinuousMemoryResource)
THUMB_FN(0xe0663894, FreeMemoryResource)
THUMB_FN(0xe0586524, AddMemoryChunk)
THUMB_FN(0xe05865a8, GetFirstChunkFromSuite)
THUMB_FN(0xe0586dc8, GetNextMemoryChunk)
THUMB_FN(0xe058636a, GetMemoryAddressOfMemoryChunk)
THUMB_FN(0xe0586774, DeleteMemorySuite)
THUMB_FN(0xe0586a00, CreateMemorySuite)
THUMB_FN(0xe0586046, CreateMemoryChunk)
// SRM use disabled via CONFIG_MEMORY_SRM_NOT_WORKING

/** Uncategorized **/
DATA_PTR(0x14684, sd_device)
THUMB_FN(0xe0818adc, _LoadCalendarFromRTC)
THUMB_FN(0xe08f942e, ErrForCamera_handler)

/** Properties **/
THUMB_FN(0xe058c8dc, _prop_cleanup)
THUMB_FN(0xe058c332, prop_register_slave)
THUMB_FN(0xe058c400, _prop_request_change)

/**
 * Things needed for CONFIG_COMPOSITOR_XCM / CONFIG_XCM_DEDICATED_LAYER
 * Via EOS RP as it uses later XCM variant than R
 */
THUMB_FN(0xe027640c, refreshVrmsSurface);           // Now called VMIX_TransferRectangleToVram
DATA_PTR(   0x11ad8, display_refresh_needed)        // Easy to spot in refreshVrmsSurface()
DATA_PTR(   0x78b74, RENDERER_LayersMetadata);
THUMB_FN(0xe009acba, XCM_SetSourceSurface);         // XCM/XOC functions have nice debug strings
THUMB_FN(0xe009af70, XCM_GetSourceSurface)
THUMB_FN(0xe009afa4, XCM_SetSourceArea);
THUMB_FN(0xe027679a, XOC_SetLayerEnable);
DATA_PTR(   0x78ce8, _pXCM);

/**
 * General FEATURE_VRAM_RGBA stubs
 *
 * _rgb_vram_info is a pointer to first Canon RGBA buffer (GUI).
 * On M6II this is set up by RENDERER_InitializeScreen(), after MARVs are created:
 * '_rgb_vram_info = RENDERER_LayersMetadata[0];'
 */
//THUMB_FN(0xe01a1366, RENDERER_InitializeScreen)   // by debug message. Not used in code, left for reference above.
THUMB_FN(0xe017cef0, XimrExe);                      // In RefreshVrmsSurface, just before "XimrExe" debug message
DATA_PTR(    0x11ac4, winsys_sem);                   // Used in RefreshVrmsSurface around XimrExe call
DATA_PTR(   0x11a8c, _rgb_vram_info);


/** App handlers **/
/**
 * Those can usually be found by looking at function address passed to
 * WINSYS_CreateDialogBox(), usually after a debug message.
 */
THUMB_FN(0xe0520974, PlayMain_handler)
THUMB_FN(0xe06ef5b0, ShootOlcApp_handler)
THUMB_FN(0xe0433e4c, LiveViewApp_handler)
THUMB_FN(0xe03a06ca, PlayMovieGuideApp_handler)
DATA_PTR(0xf0b4, LiveViewApp_dialog)

// Not used at this moment
//THUMB_FN(0xe0524ad8, DISP_SetHighLight)
