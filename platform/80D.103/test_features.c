/** \file
 * Temporary tests for features, specific for 80D.1-3
 *
 * When porting to a new model, using this camera as a base, remove this file
 * and remove it from ML_SRC_EXTRA_OBJS in Makefile!
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

#ifndef CONFIG_HELLO_WORLD
#include <dryos.h>
#include <bmp.h>



extern uint32_t SetPreDisplayType(uint32_t type_flags);
extern uint32_t SetDisplayType(uint32_t type_flags);
extern uint32_t GetDisplayType();


static MENU_UPDATE_FUNC(display_mode)
{
//    uint32_t sensor_id = (uint32_t) entry->priv;
//    if (sensor_id >= sizeof(eosr_temperatures))
//      return;

    MENU_SET_VALUE("%08x", GetDisplayType());
}

#define D6_HW_LAYER_INDEX 0x16
static void buf_test()
{
    uint32_t img_width = 960;
    uint32_t img_height = 540;
    uint32_t img_mask = ((img_width << 0x10) >> 0x14) - 1 | 0x20000;
    DryosDebugMsg(0, 15, "bmp params %08x %08x %08x", img_width, img_height, img_mask);
  //  DryosDebugMsg(0, 15, "bmp_vram_indexed_d6 %08x", bmp_vram_indexed_d6);

    uint32_t old_int = cli();
    // enable 8th layer,
    // flags, whatever this means, 0x1 is for "enable" and 0x340 is for "indexed RGB"
    // flags found in bootloader.
    *((volatile uint32_t *)0xd20138f0) = 0x349;
    // this seems to be the magic that enables 0x16 entry, as pushed into "layer vram MMIO" later.
    *((volatile uint32_t *)0xd20138f4) = D6_HW_LAYER_INDEX << 8;
    // input offsets
    *((volatile uint32_t *)0xd20138f8) = -BMP_W_MINUS | (-BMP_H_MINUS << 16);
    // resolution
    *((volatile uint32_t *)0xd20138fc) = (BMP_W_PLUS - BMP_W_MINUS) | ((BMP_H_PLUS - BMP_H_MINUS ) << 16);
    // output offsets
    *((volatile uint32_t *)0xd2013900) = 0x0;


    // our layer "index"
    *((volatile uint32_t *)0xd2030100) = D6_HW_LAYER_INDEX;
    // pitch, calculated in some weird bithsift wai
    *((volatile uint32_t *)0xd2030104) = 0x2003b;
    // finally, the buffer address. It has to be rsh 8 bytes.
    // Many things (Ximr included) on d6/7 needs 0x100 alignment so HW limitation I guess?
  //  *((volatile uint32_t *)0xd2030108) = (uint32_t)bmp_vram_indexed >> 8;
    sei(old_int);
}

static void display_type_toggle()
{
    uint32_t current_flags = GetDisplayType();
  //  uint32_t current_lcd = current_flags & 0x1;
  //  uint32_5 current_hdmi = current_frags & 0xF00;
    uint32_t next_flags = 0x1;
    if(current_flags == 0x1) next_flags = 0x300;
    if(current_flags == 0x300) next_flags = 0x301;
    if(current_flags == 0x301) next_flags = 0x1;
  //  if(current_flags == 0x501) next_flags = 0x1;

    DryosDebugMsg(0, 15, "Toggle display mode");
    //SetPreDisplayType(next_flags);
    //msleep(100); //not sure if fn above needs a wait? msgs are queued so this shall execute in order...
    SetDisplayType(next_flags);
}

static struct menu_entry test_features_debug_menu[] = {
    {
        .name   = "80D test features",
        .select = menu_open_submenu,
        .help   = "Temporary features, not integrated into main codebase yet.",
        .submenu_width = 710,
        .children =  (struct menu_entry[]) {
            {
                .name   = "buf test",
                .priv   = buf_test,
                .select = run_in_separate_task,
            },
            {
                .name   = "Toggle HDMI modes",
                .priv   = display_type_toggle,
                .select = run_in_separate_task,
            },
            {
                .name   = "Current display mode",
                .priv   = (int*)0,
                .update = display_mode,
                .icon_type = IT_ALWAYS_ON
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
