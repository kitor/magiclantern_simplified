/** \file
 * Minimal test code for DIGIC 7 & 8
 * ROM dumper & other experiments
 */

#include "dryos.h"
#include "version.h"
#include "bmp.h"
#include "fw-signature.h"

extern void dump_file(char* name, uint32_t addr, uint32_t size);
extern void memmap_info(void);
extern void malloc_info(void);
extern void sysmem_info(void);
extern void smemShowFix(void);

static int _hold_your_horses = 1; // 0 after config is read
int ml_started = 0; // 1 after ML is fully loaded
int ml_gui_initialized = 0; // 1 after gui_main_task is started

/* stolen from debug.c */

void _card_led_on()
{
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = (LEDON);
}

void _card_led_off()
{
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = (LEDOFF);
}

void info_led_on()
{
    #ifdef CONFIG_VXWORKS
    LEDBLUE = LEDON;
    #elif defined(CONFIG_BLUE_LED)
    call("EdLedOn");
    #else
    _card_led_on();
    #endif
}
void info_led_off()
{
    #ifdef CONFIG_VXWORKS
    LEDBLUE = LEDOFF;
    #elif defined(CONFIG_BLUE_LED)
    call("EdLedOff");
    #else
    _card_led_off();
    #endif
}
void info_led_blink(int times, int delay_on, int delay_off)
{
    for (int i = 0; i < times; i++)
    {
        info_led_on();
        msleep(delay_on);
        info_led_off();
        msleep(delay_off);
    }
}

extern int uart_printf(const char * fmt, ...);

static void hello_world()
{
    _mem_init();
    _find_ml_card();
    _load_fonts();

    int sig = compute_signature((int*)SIG_START, 0x10000);
    while(1)
    {
        bmp_printf(FONT_LARGE, 50, 50, "Yo, man!");
        bmp_printf(FONT_LARGE, 50, 400, "firmware signature = 0x%x", sig);
        info_led_blink(1, 500, 500);
    }
}

/* This runs ML initialization routines and starts user tasks.
 * Unlike init_task, from here we can do file I/O and others.
 */
static void my_big_init_task()
{
    hello_world();
    return;
}

/* called before Canon's init_task */
void boot_pre_init_task(void)
{
    /* nothing to do */
}

/* called right after Canon's init_task, while their initialization continues in background */
void boot_post_init_task(void)
{
    msleep(1000);

    #if !defined(CONFIG_NO_ADDITIONAL_VERSION)
        // Re-write the version string.
        // Don't use strcpy() so that this can be done
        // before strcpy() or memcpy() are located.
        extern char additional_version[];
        additional_version[0] = '-';
        additional_version[1] = 'm';
        additional_version[2] = 'l';
        additional_version[3] = '-';
        additional_version[4] = build_version[0];
        additional_version[5] = build_version[1];
        additional_version[6] = build_version[2];
        additional_version[7] = build_version[3];
        additional_version[8] = build_version[4];
        additional_version[9] = build_version[5];
        additional_version[10] = build_version[6];
        additional_version[11] = build_version[7];
        additional_version[12] = build_version[8];
        additional_version[13] = '\0';
    #endif

    // wait for firmware to initialize
    while (!bmp_vram_raw()) msleep(100);

    task_create("ml_init", 0x1e, 0x4000, my_big_init_task, 0 );
}

/** Dummies **/
void ml_assert_handler(char* msg, char* file, int line, const char* func) {};
void bvram_mirror_init(){};
void _update_vram_params(){};
void redraw_after(int time) {}
int hdmi_code = 0;
void draw_line(int x1, int y1, int x2, int y2, int cl){}
void beep() {} ;
void EngDrvOut(uint32_t reg, uint32_t value) {}
int printf(const char * format, ...) { return 0; }
int y_times_BMPPITCH_cache[BMP_H_PLUS - BMP_H_MINUS];


/** kill exmem */
struct memSuite * _shoot_malloc_suite(size_t size) { return 0; }
struct memSuite * _shoot_malloc_suite_contig(size_t size) { return 0; }
void _shoot_free_suite(struct memSuite * suite) {}
struct memSuite * _srm_malloc_suite(int num) { return 0; }
void _srm_free_suite(struct memSuite * suite) {}
void* _shoot_malloc(size_t size) { return (void*)0x0; }
void _shoot_free(void *ptr) { return; }
