/**
 * Camera internals for EOS M50 1.1.0
 */

/** This camera has a DIGIC VIII chip */
#define CONFIG_DIGIC_VIII

// has inter-core RPC (so far this has always been dependent on SGI, 0xc)
#define CONFIG_RPC

// Cam has MMU (by itself, does nothing, see CONFIG_MMU_REMAP)
#define CONFIG_MMU

/** Digic 8 does not have bitmap font in ROM, try to load it from card **/
#define CONFIG_NO_BFNT

/** disable SRM for now
 * in current state SRM_AllocateMemoryResourceFor1stJob makes camera crash
 * even if just one buffer is requrested.
 */
#define CONFIG_MEMORY_SRM_NOT_WORKING

/* has LV */
#define CONFIG_LIVEVIEW

/* enable LiveView RAW backend (required by raw video modules to get non-zero raw_info geometry) */
#define CONFIG_RAW_LIVEVIEW

/* EVF_STATE hooks: confirmed working on M50 without Error 70.
 * CONFIG_STATE_OBJECT_HOOKS: hooks the EVF_STATE spy function.
 * CONFIG_EVF_STATE_SYNC: calls vsync_func() on input==5 && old_state==5
 *   (evfReadOutDoneInterrupt), which fires module_exec_cbr(CBR_VSYNC).
 * This gives frame-accurate vsync from Canon's own pipeline. */
#define CONFIG_STATE_OBJECT_HOOKS
#define CONFIG_EVF_STATE_SYNC

/* M50 has no mode dial — movie mode is selected via touchscreen menu.
 * Without this, is_movie_mode() checks shooting_mode == 0x14, which
 * never matches on M50.  With this flag it checks lv_movie_select instead. */
#define CONFIG_NO_DEDICATED_MOVIE_MODE

#define CONFIG_MALLOC_STRUCT_V2

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5
