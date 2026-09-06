#include "dryos.h"
#include "fps-engio_per_cam.h"

/*
 * M50/DIGIC 8: shamem_read is dead on all MMIO addresses,
 * so we can't read the actual FPS timer registers.
 *
 * NOTE: TG_FREQ_BASE=32000000 in fps-engio.c is a blind copy from 700D
 * (DIGIC 5).  DIGIC 7/8 cameras use higher values:
 *   200D = 84 MHz, 6D2 = 66.8 MHz, R/RP = unknown.
 * The real M50 TG clock is probably 40-84 MHz.
 *
 * Return values that produce ~30fps with TG_FREQ_BASE=32000000:
 *   timerA = reg_a + 1 = 490
 *   tg_freq = calc_tg_freq(490) ≈ 65,306,122
 *   timerB = reg_b + 1 = 2177
 *   fps_x1000 = 65306122 / 2177 ≈ 29998 (~30.0 fps)
 *
 * These are self-consistent fabricated values. FPS override cannot
 * work until the real DIGIC 8 timer registers (likely 0xD0F0xxxx
 * region) are identified and TG_FREQ_BASE is measured.
 */
int get_fps_register_a(void)
{
//    return shamem_read(FPS_REGISTER_A);
    return 489;   /* timerA = 490, avoids integer overflow in calc_tg_freq */
}

int get_fps_register_a_default(void)
{
//    return shamem_read(FPS_REGISTER_A + 4);
    return 489;
}

int get_fps_register_b(void)
{
//    return shamem_read(FPS_REGISTER_B);
    return 2176;  /* timerB = 2177, gives ~30fps with timerA=490 */
}
