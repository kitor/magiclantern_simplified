#ifndef _picquality_h_
#define _picquality_h_

// True only until Digic 8. To be reworked.
#define PICQ_RAW                 0x4060000 // tweaks.c, raw.c
//#define PICQ_MRAW                0x4060001
//#define PICQ_SRAW                0x4060002
#define PICQ_RAW_JPG_LARGE_FINE  0x3070000 // tweaks.c
//#define PICQ_MRAW_JPG_LARGE_FINE 0x3070001
//#define PICQ_SRAW_JPG_LARGE_FINE 0x3070002
#define PICQ_RAW_JPG_MED_FINE    0x3070100
//#define PICQ_MRAW_JPG_MED_FINE   0x3070101
//#define PICQ_SRAW_JPG_MED_FINE   0x3070102
#define PICQ_RAW_JPG_SMALL_FINE  0x3070200
//#define PICQ_MRAW_JPG_SMALL_FINE 0x3070201
//#define PICQ_SRAW_JPG_SMALL_FINE 0x3070202
#define PICQ_LARGE_FINE          0x3010000  // tweaks.c
//#define PICQ_LARGE_COARSE        0x2010000
//#define PICQ_MED_FINE            0x3010100
//#define PICQ_MED_COARSE          0x2010100
//#define PICQ_SMALL_FINE          0x3010200
//#define PICQ_SMALL_COARSE        0x2010200

/*
TODO:
Make some sensible getters so code can get if it is RAW, MRAW, SRAw, CRAW or JPEG.


-> property.c
Move `PROP_PIC_QUALITY`... handlers to picquality.c making them private.
We already established changing those may soft brick cam.

-> tweaks.c
Part of FEATURE_WARNINGS_FOR_BAD_SETTINGS
User can enable warnings depending on mode. Rewrite to getters.

-> raw.c
has can_use_raw_overlays_photo() that checks if MRAW/SRAW is enabled
It also begs for a getter function. And check if D8+ CRAW has the same issue

in can_use_raw_overlays_menu() there's a similar check that allows sraw/mraw

-> picstyle.c
int jpg = pic_quality & 0x10000;
that should get getter is_jpeg()

-> zebra.c
int raw = pic_quality & 0x60000;
another raw check, lol.


Modules:
-> dual_iso.c
uses the same check as `can_use_raw_overlays_photo`
(pic_quality & 0xFE00FF) == (PICQ_RAW & 0xFE00FF), thus whatever works for raw.c
should also work there

-> lua/lua_dryos.c
has `int raw = pic_quality & 0x60000;` which could use picquality_is_raw()

-> ettr/ettr.c:
int raw = is_movie_mode() ? raw_lv_is_enabled() : pic_quality & 0x60000;
like lua?

-> raw_video/mlv_lite/mlv_lite.c:
changing video mode or picture quality
 * may also require reallocation
 * squeeze everything here (a bit hackish)
//current_state ^= (pic_quality << 4);
//current_state ^= (video_mode_resolution << 8);
//current_state ^= (video_mode_fps << 16);
//current_state ^= (video_mode_crop << 24);
That's wonderful. What to do with this?!



*/

#endif // _picquality_h_
