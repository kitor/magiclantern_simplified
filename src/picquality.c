#include <lvinfo.h>
#include <property.h>

/*
 * Picture quality related functions.
 * They use PROP_PIC_QUALITY, PROP_PIC_QUALITY2, PROP_PIC_QUALITY3
 *
 * Each prop is supposed to cover one of the available storages:
 * ~Alex, 2015:
 * > I took a closer look in Canon code - it appears to set all 3 of them,
 * > and from GUI_SetImgComposition, the first property seems to be for
 * > drive A (CF), second for drive B (SD), and third for drive C (WFT maybe).
 *
 * There was a broken feature FEATURE_PICQ_DANGEROUS that got removed.
 * It just added quality selection to our menu in a broken way that could soft
 * brick the camera.
 *
 * ~Alex, 2015:
 * >  The danger in this feature is because some picture quality values valid
 * >  on one camera may soft-brick another (or result in corrupted files
 * >  or corrupted Canon GUI). However, I consider this type of bricking
 * >  as relatively easy to recover (on 550D it's enough to call set_pic_quality(PICQ_RAW),
 * >  on 60D... disable asserts and clear camera settings).
 *
 * Source: https://www.magiclantern.fm/forum/index.php?topic=14246.0
 */

static LVINFO_UPDATE_FUNC(picq_update)
{
    LVINFO_BUFFER(16);

    if (!is_movie_mode())
    {
#if defined(CONFIG_DIGIC_8X)
/* via R.180, confirmed RP.160; M50 and 850D have the same set of modes:
 * TODO: Verify cams like sx740 that have no RAW option in menu but it works anyway
 * L        03030100   .XX .... ..XX .... ...X .... ....
 * l        03020100   .XX .... ..X. .... ...X .... ....
 * M        03030101   .XX .... ..XX .... ...X .... ...X
 * m        03020101   .XX .... ..X. .... ...X .... ...X
 * S1       0303010E   .XX .... ..XX .... ...X .... XXX.
 * s1       0302010E   .XX .... ..X. .... ...X .... XXX.
 * S2       0303010F   .XX .... ..XX .... ...X .... XXXX
 * CRAW     03030600   .XX .... ..XX .... .XX. .... ....
 * RAW      04030600   X.. .... ..XX .... .XX. .... ....
 * via SX740.110, there's only one S mode:
 * S        03030102   .XX .... ..XX .... ...X .... ..X.
 * Some combinations:
 *  RAW + L 04030700   X.. .... ..XX .... .XXX .... ....
 *  RAW + l 04020700   X.. .... ..X. .... .XXX .... ....
 * CRAW + L 03030700   .XX .... ..XX .... .XXX .... ....
 * CRAW + l 03020700   .XX .... ..X. .... .XXX .... ....
 */
        int raw = pic_quality & 0x600; // 3 for *RAW, 0 - not RAW
        int jpg = (pic_quality & 0x100);
        int rawsize = pic_quality >> 26; // if raw: 0 CRAW, 1 RAW
        int jpegtype = (pic_quality >> 16) & 0xF;
        int jpegsize = pic_quality & 0xFF;
#else
        int raw = pic_quality & 0x60000;
        int jpg = pic_quality & 0x10000;
        int rawsize = pic_quality & 0xF;
        int jpegtype = pic_quality >> 24;
        int jpegsize = (pic_quality >> 8) & 0xFF;
#endif //CONFIG_DIGIC_VIII + CONFIG_DIGIC_X
        snprintf(buffer, sizeof(buffer), "%s%s%s",
#if defined(CONFIG_DIGIC_8X)
            raw ? (rawsize ? "RAW" : "CRAW") : "",  // just two options on D8
#else
            rawsize == 1 ? "mRAW" : rawsize == 2 ? "sRAW" : rawsize == 7 ? "sRAW1" : rawsize == 8 ? "sRAW2" : raw ? "RAW" : "",
#endif
            jpg == 0 ? "" : (raw ? "+" : "JPG-"),
            jpg == 0 ? "" : (
                jpegsize == 0 ? (jpegtype == 3 ? "L" : "l") :
                jpegsize == 1 ? (jpegtype == 3 ? "M" : "m") :
                jpegsize == 2 ? (jpegtype == 3 ? "S" : "s") :
                jpegsize == 0x0e ? (jpegtype == 3 ? "S1" : "s1") :
                jpegsize == 0x0f ? (jpegtype == 3 ? "S2" : "s2") :
                jpegsize == 0x10 ? (jpegtype == 3 ? "S3" : "s3") :
                "err"
            )
        );
    }

    int raw_lv = raw_lv_is_enabled();
    if (raw_lv)
    {
        /* make it obvious that LiveView is in RAW mode */
        /* (primarily for troubleshooting the raw backend, proper raw_lv_request/release calls and Magic Zoom slowdowns) */
        if (is_movie_mode())
        {
            /* todo: icon? */
            snprintf(buffer, sizeof(buffer), "RAW");
        }
        item->color_fg = raw_lv == 1 ? COLOR_GREEN1 : COLOR_GRAY(20);
    }
}

static struct lvinfo_item info_item = {
    .name = "Pic.Style",
    .which_bar = LV_TOP_BAR_ONLY,
    .update = picq_update,
    .preferred_position = 3,
};

static void picquality_init()
{
    lvinfo_add_item(&info_item);
}

INIT_FUNC(__FILE__, picquality_init);
