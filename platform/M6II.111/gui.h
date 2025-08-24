#ifndef _cameraspecific_gui_h_
#define _cameraspecific_gui_h_

/* As in R180 */


#define BGMT_WHEEL_UP                0x00
#define BGMT_WHEEL_DOWN              0x01
#define BGMT_WHEEL_LEFT              0x02
#define BGMT_WHEEL_RIGHT             0x03

/* Two thumb wheels are defined. Shutter wheel is codes 0x8 / 0x9 */
#define BGMT_PRESS_SET               0x0A
#define BGMT_UNPRESS_SET             0x0B

#define BGMT_MENU                    0x0C // unpress D
#define BGMT_INFO                    0x0E // unpress 0xF

#define BGMT_PLAY                    0x12 // I think?
#define BGMT_TRASH                   0x21 // map to M-fn for now

#define BGMT_PRESS_RIGHT             0x3B
#define BGMT_UNPRESS_RIGHT           0x3C
#define BGMT_PRESS_LEFT              0x3D
#define BGMT_UNPRESS_LEFT            0x3E
#define BGMT_PRESS_UP                0x3F
#define BGMT_UNPRESS_UP              0x40
#define BGMT_PRESS_DOWN              0x41
#define BGMT_UNPRESS_DOWN            0x42


#define BGMT_PRESS_HALFSHUTTER       0x9C // unpress 9D

// needed for correct shutdown from powersave modes
#define GMT_GUICMD_LOCK_OFF          0xAC // GUICMD_LOCK_OFF
#define GMT_GUICMD_START_AS_CHECK    0xB3 // GUICMD_START_AS_CHECK
#define GMT_OLC_INFO_CHANGED         0xB4 // copyOlcDataToStorage uiCommand(%d)

/* kitor: Defs not used by ML
 * MODE button: 0x35 PRESS, 0x36 UNPRESS
 * Backlight:   0x3D PRESS, 0x3E UNPRESS
 * LOCK:        0x92 LOCK , 0x93 UNLOCK
 * RECORD:      0x21 PRESS, 0x22 UNPRESS
 * M-Fn:        0x1A PRESS, 0x1B UNPRESS
 * AF ON:       0x81 PRESS, 0x82 UNPRESS
 * Star:        0x85 PRESS
 * Zoom/AF sel: 0x15 PRESS, 0x16 UNPRESS
 * Touch bar:   Between 0x4F and 0x56:
 * - tap R      0x4F tap
 * - lift R     0x50 lift (finger up on the right side)
 * - tap L      0x51 tap
 * - lift L     0x52 lift (finger up on the left side)
 * - swipe R    0x55 repeats while moving finger in right direction
 * - swipe L    0x56 repeats while moving finger in left direction
 * "lift" events are generated both for taps and swipes. "swipe" events repeats
 * based on distance traveled in given direction. One can swipe left and right,
 * and correct events will be generated - like moving finger on laptop touchpad.
 */

/* Codes below are WRONG: DNE in R */
#define BGMT_LV                      -0xF3 // MILC, always LV.

#define BGMT_PRESS_ZOOM_IN           -0xF4
#define GMT_GUICMD_OPEN_SLOT_COVER   -0xF5 // N/A on this model
//#define BGMT_UNPRESS_ZOOM_IN         -0xF5
//#define BGMT_PRESS_ZOOM_OUT          -0xF6
//#define BGMT_UNPRESS_ZOOM_OUT        -0xF7
#endif
