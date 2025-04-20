/**
 * Camera internals for 77D 1.1.0
 */

/** This camera has a DIGIC VII chip */
#define CONFIG_DIGIC_VII

/** Digic 7 does not have bitmap font in ROM, try to load it from card **/
#define CONFIG_NO_BFNT

/* has LV */
#define CONFIG_LIVEVIEW

/* enable state objects hooks */
#define CONFIG_STATE_OBJECT_HOOKS

// SRM is untested, this define is to allowing building
// without SRM_BUFFER_SIZE being found
#define CONFIG_MEMORY_SRM_NOT_WORKING

#define CONFIG_MALLOC_STRUCT_V2

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5

// has inter-core RPC (so far this has always been dependent on SGI, 0xc)
#define CONFIG_RPC

// Cam has MMU (by itself, does nothing, see CONFIG_MMU_REMAP)
#define CONFIG_MMU

// Cam can wrap init1, allowing control of cpu1 before tasks are started
#define CONFIG_INIT1_HIJACK

// This camera loads ML into the AllocateMemory pool
#define CONFIG_ALLOCATE_MEMORY_POOL