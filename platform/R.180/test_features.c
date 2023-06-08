/** \file
 * Temporary tests for features, specific for R 1.8.0
 *
 * When porting to a new model, using this camera as a base, remove this file
 * and remove it from ML_SRC_EXTRA_OBJS in Makefile.platform.default!
 */
/*
 * Copyright (C) 2022 Magic Lantern Team
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

extern void DISP_SetHighLight(int);
int highlight_flag = 0;

extern int uart_printf(const char * fmt, ...);
extern uint32_t * mzrm_CreateMsg(void * RPC, uint32_t msg_type, uint32_t function_id, size_t msg_size);
extern uint32_t mzrm_SendMsg(void * RPC, uint32_t * msg);
extern void mzrm_GiveSemaphore(void * RPC, uint32_t *msg);
extern void * pMzrmRPC;

uint32_t eglGetDisplay(uint32_t display_id)
{
  uint32_t * msg = mzrm_CreateMsg(pMzrmRPC,3,183,4);
  if (msg != NULL) {
    msg[3] = display_id;
    uart_printf(" mzrm eglGetDisplay %d\n", msg[3]);
    uint32_t val = mzrm_SendMsg(pMzrmRPC,msg);
    mzrm_GiveSemaphore(pMzrmRPC,msg);
    return val;
  }
  return 0;
}

uint32_t eglGetError(void)
{
  uint32_t * msg = mzrm_CreateMsg(pMzrmRPC,3,182,0);
  if (msg != NULL) {
    uart_printf(" mzrm eglGetError\n");
    uint32_t val = mzrm_SendMsg(pMzrmRPC,msg);
    mzrm_GiveSemaphore(pMzrmRPC,msg);
    return val;
  }
  return 0;
}

uint32_t eglInitialize(uint32_t dpy, uint32_t *major, uint32_t *minor)
{
  uint32_t * msg = mzrm_CreateMsg(pMzrmRPC,3,184,12);
  if (msg != NULL) {
    msg[3] = dpy;
    msg[4] = (uint32_t *)major;
    msg[5] = (uint32_t *)minor;
    uart_printf(" mzrm eglInitialize %d %d %d\n", msg[3], msg[4], msg[5]);
    uint32_t val = mzrm_SendMsg(pMzrmRPC,msg);
    mzrm_GiveSemaphore(pMzrmRPC,msg);
    return val;
  }
  return 0;
}

uint32_t  eglTerminate(uint32_t dpy)
{
  uint32_t * msg = mzrm_CreateMsg(pMzrmRPC,3,185,4);
  if (msg != NULL) {
    msg[3] = dpy;
    uart_printf(" mzrm eglTerminate %d\n", msg[3]);
    uint32_t val = mzrm_SendMsg(pMzrmRPC,msg);
    mzrm_GiveSemaphore(pMzrmRPC,msg);
    return val;
  }
  return 0;
}

// ZICO private region is at 0x80000000 zico-side
// Calculate offset to fetch strings from ICU ROM instead
#define ZICO_FIRMWARE_OFFSET (0xe0de4c8c - 0x80000000)

uint32_t eglQueryString(uint32_t dpy, uint32_t name)
{
  uint32_t * msg = mzrm_CreateMsg(pMzrmRPC,3,186,8);
  if (msg != NULL) {
    msg[3] = dpy;
    msg[4] = name;
    uart_printf(" mzrm eglQueryString %d\n", msg[3]);
    uint32_t val = mzrm_SendMsg(pMzrmRPC,msg);
    val += ZICO_FIRMWARE_OFFSET;
    mzrm_GiveSemaphore(pMzrmRPC,msg);
    return val;
  }
  return 0;
} 

// QueryString targets ..
#define EGL_VENDOR       0x3053
#define EGL_VERSION      0x3054
#define EGL_EXTENSIONS   0x3055
#define EGL_CLIENT_APIS  0x308D

#define EGL_DEFAULT_DISPLAY 0

static void egl_test()
{
  uint32_t dpy = eglGetDisplay( EGL_DEFAULT_DISPLAY );
  //uart_printf("init failed %08x\n", eglGetError());

  uint32_t major, minor;
  if(!eglInitialize(dpy, &major, &minor)){
      uart_printf("init failed %08x\n", eglGetError() );
      return;
  } 
  uart_printf("cmd status %08x\n", eglGetError());
  uart_printf("EGL Version %d.%d\n", major, minor);
  uart_printf("EGL_VENDOR %s\n", eglQueryString(dpy, EGL_VENDOR));
  uart_printf("EGL_VERSION %s\n",     eglQueryString(dpy, EGL_VERSION ) );
  uart_printf("EGL_EXTENSIONS %s\n",  eglQueryString(dpy, EGL_EXTENSIONS) );
  uart_printf("EGL_CLIENT_APIS %s\n", eglQueryString(dpy, EGL_CLIENT_APIS ) );
  //eglTerminate(dpy);
  //uart_printf("cmd status %08x\n", eglGetError());
}



static void overexpo_toggle()
{
    highlight_flag = !highlight_flag;
    msleep(1000); //let the code execute when user is already back in LV
    DryosDebugMsg(0, 15, "Toggle overexposure warning in LV: %d", highlight_flag);
    DISP_SetHighLight(highlight_flag);
}

static struct menu_entry test_features_debug_menu[] = {
    {
        .name   = "EOS R Test features",
        .select = menu_open_submenu,
        .help   = "Temporary features, not integrated into main codebase yet.",
        .children =  (struct menu_entry[]) {
            {
                .name   = "Overexposure warning in LiveView",
                .priv   = overexpo_toggle,
                .select = run_in_separate_task,
                .help   = "You may need to toggle twice each time you enter LV."
            },
            {
                .name   = "Test EGL",
                .priv   = egl_test,
                .select = run_in_separate_task,
                .help   = "test MZRM EGL"
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
