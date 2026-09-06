/*
 * ROM Dump Module for Canon M50 (DIGIC 8)  — v3.0
 *
 * Dumps decrypted firmware ROM to binary files on SD card.
 * DIGIC 8 decrypts ROM into memory at runtime, so we can read it
 * directly from the mapped addresses.
 *
 * Memory map:
 *   ROM0: 0xE0000000 - 0xE1FFFFFF (32 MB, main firmware)
 *   ROM1: 0xF0000000 - 0xF0FFFFFF (16 MB, secondary)
 *   BOOT: 0xDF000000 - 0xDF00FFFF (64 KB, bootloader)
 *
 * Output:
 *   ML/LOGS/ROM0.BIN  (32 MB)
 *   ML/LOGS/ROM1.BIN  (16 MB)
 *   ML/LOGS/BOOT.BIN  (64 KB)
 *   ML/LOGS/RD_STAT.LOG (progress log)
 *
 * v4.0 — fix: Canon's _FIO_WriteFile on DIGIC 8 requires uncacheable
 *   addresses (same as 850D/R/M6II/RP with CONFIG_MEM_2GB).
 *   CPU-copy ROM data into UNCACHEABLE alias of staging buffer,
 *   then pass that uncacheable pointer to FIO_WriteFile.
 */

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <notify_box.h>

/* Cacheable/uncacheable conversion — DIGIC 8 uses 0x40000000 bit */
#ifndef UNCACHEABLE
#define UNCACHEABLE(x) ((void*)(((uint32_t)(x)) | (((uint32_t)(x)) < 0x40000000 ?  0x40000000 : 0)))
#endif

/* Chunk size for writes — 32 KB at a time */
#define CHUNK_SIZE  (32 * 1024)

/* Static RAM staging buffer — CPU copies ROM here before FIO_WriteFile.
 * FIO_WriteFile uses DMA which can't access MMU-mapped ROM addresses. */
static uint8_t staging_buf[CHUNK_SIZE] __attribute__((aligned(64)));

/* ROM regions */
struct rom_region {
    uint32_t    start;
    uint32_t    size;
    const char *name;
    const char *filename;
};

static struct rom_region regions[] = {
    { 0xE0000000, 0x02000000, "ROM0", "ML/LOGS/ROM0.BIN" },  /* 32 MB */
    { 0xF0000000, 0x01000000, "ROM1", "ML/LOGS/ROM1.BIN" },  /* 16 MB */
    { 0xDF000000, 0x00010000, "BOOT", "ML/LOGS/BOOT.BIN" },  /*  64 KB */
};

#define NUM_REGIONS (sizeof(regions) / sizeof(regions[0]))

/* Log file handle */
static FILE *logfile = NULL;

static void log_msg(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Print to ML console */
    printf("%s\n", buf);

    /* Write to log file */
    if (logfile)
    {
        int len = strlen(buf);
        buf[len] = '\n';
        FIO_WriteFile(logfile, buf, len + 1);
    }
}

/* Check if an address is readable (non-MMIO) */
static int addr_is_safe(uint32_t addr)
{
    uint32_t top4 = addr >> 28;
    /* RAM */
    if (top4 == 0x0 || top4 == 0x1 || top4 == 0x4 || top4 == 0x5)
        return 1;
    /* ROM */
    if (top4 >= 0xE)
        return 1;
    /* Bootloader */
    if (addr >= 0xDF000000 && addr < 0xE0000000)
        return 1;
    return 0;
}

/* Dump one region to a file.
 * FIO_WriteFile uses DMA internally, and the DMA controller can't access
 * the MMU-mapped ROM addresses on DIGIC 8. So we must CPU-copy ROM data
 * into a RAM staging buffer first, then write the buffer to file.
 */
static int dump_region(struct rom_region *r, void *buf)
{
    log_msg("=== Dumping %s: 0x%08X, %d KB ===", r->name, r->start, r->size / 1024);

    /* Validate address range */
    if (!addr_is_safe(r->start) || !addr_is_safe(r->start + r->size - 1))
    {
        log_msg("  SKIP: unsafe address range");
        return -1;
    }

    /* Quick sanity check — read first word */
    uint32_t first_word = *(volatile uint32_t *)r->start;
    log_msg("  First word: 0x%08X", first_word);

    /* Create the output file */
    FILE *f = FIO_CreateFile(r->filename);
    if (!f)
    {
        log_msg("  ERROR: Cannot create %s", r->filename);
        return -1;
    }

    uint32_t written = 0;
    uint32_t addr = r->start;

    while (written < r->size)
    {
        uint32_t chunk = r->size - written;
        if (chunk > CHUNK_SIZE)
            chunk = CHUNK_SIZE;

        /* CPU-copy from ROM into UNCACHEABLE alias of RAM buffer.
         * Writing through the uncacheable alias (0x40xxxxxx) bypasses
         * CPU cache entirely — data goes straight to physical RAM.
         * Canon's _FIO_WriteFile on DIGIC 8 also requires an uncacheable
         * address (same as 850D/R/M6II/RP with CONFIG_MEM_2GB). */
        volatile uint32_t *src = (volatile uint32_t *)addr;
        uint32_t *dst = (uint32_t *)UNCACHEABLE(buf);
        uint32_t words = chunk / 4;
        for (uint32_t i = 0; i < words; i++)
        {
            dst[i] = src[i];
        }

        /* Write from uncacheable alias — DMA-safe */
        int ret = FIO_WriteFile(f, (void *)UNCACHEABLE(buf), chunk);
        if (ret != (int)chunk)
        {
            log_msg("  WRITE ERROR at 0x%08X: expected %d, got %d",
                    addr, chunk, ret);
            break;
        }

        written += chunk;
        addr += chunk;

        /* Progress update every 1 MB */
        if ((written % (1024 * 1024)) == 0 || written == r->size)
        {
            int pct = (written / 1024) * 100 / (r->size / 1024);
            log_msg("  %s: %d / %d KB (%d%%)",
                    r->name, written / 1024, r->size / 1024, pct);
            NotifyBox(2000, "ROM Dump: %s %d%%", r->name, pct);
        }

        /* Yield to avoid watchdog */
        msleep(10);
    }

    FIO_CloseFile(f);
    log_msg("  %s: done, %d bytes written to %s", r->name, written, r->filename);
    return (written == r->size) ? 0 : -1;
}

static void romdump_task(void *unused)
{
    /* Wait for camera to settle */
    msleep(3000);

    /* Check if dump already exists — skip only if ROM0.BIN has correct size (32 MB) */
    uint32_t sz = 0;
    if (FIO_GetFileSize("ML/LOGS/ROM0.BIN", &sz) == 0 && sz == 0x02000000)
    {
        NotifyBox(3000, "ROM dump exists, skipping.\nDelete ML/LOGS/ROM*.BIN to re-dump.");
        return;
    }

    NotifyBox(5000, "ROM Dump starting...\n~49 MB, please wait");

    /* Open log file */
    logfile = FIO_CreateFile("ML/LOGS/RD_STAT.LOG");

    log_msg("ROM Dump Module v4.0 (UNCACHEABLE fix)");
    log_msg("DIGIC 8 decrypted ROM dumper");
    log_msg("staging_buf at 0x%08X, chunk size %d", (uint32_t)staging_buf, CHUNK_SIZE);

    int success = 0;
    int failed = 0;

    for (uint32_t i = 0; i < NUM_REGIONS; i++)
    {
        int ret = dump_region(&regions[i], staging_buf);
        if (ret == 0)
            success++;
        else
            failed++;
    }

    log_msg("=== COMPLETE: %d OK, %d failed ===", success, failed);

    if (logfile)
    {
        FIO_CloseFile(logfile);
        logfile = NULL;
    }

    NotifyBox(10000, "ROM Dump complete!\n%d regions dumped", success);
}

static unsigned int romdump_init()
{
    task_create("romdump", 0x1e, 0x4000, romdump_task, 0);
    return 0;
}

static unsigned int romdump_deinit()
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(romdump_init)
    MODULE_DEINIT(romdump_deinit)
MODULE_INFO_END()
