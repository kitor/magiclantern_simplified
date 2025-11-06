#include <dryos.h>
#include <lens.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <picstyle.h>
#include <lvinfo.h>
#include <raw.h>

/*
 * FEATURE_PICSTYLE code, incl. FEATURE_REC_PICSTYLE
 */

#ifdef FEATURE_PICSTYLE

/* Some background:
 * Canon cameras allow user to select "picture styles". Those contain a few
 * adjustments to jpg/h26x image pipelines for sharpness, color tone, contrast
 * and saturation.
 * Each style can be adjusted, Monochrome is special (lack of color to adjust).
 *
 * There are three "User" style slots that can be either based on other styles,
 * or entire custom LUTs loaded from EOS Utility.
 *
 * Auto picstyle was introduced in late DIGIC 4 models.
 * During DIGIC 6 era Canon added Fine Detail style. They split Sharpness into
 * three sliders at the same time.
 *
 * Digic 8 changed some property IDs (see property.h)
 */


/*
 * Data types needed for picture style feature to work
 */

typedef enum
{
    STD_ID        = 0x81,
    PORTRAIT_ID   = 0x82,
    LANDSCAPE_ID  = 0x83,
    NEUTRAL_ID    = 0x84,
    FAITHFUL_ID   = 0x85,
    MONO_ID       = 0x86,
    USER1_ID      = 0x21,
    USER2_ID      = 0x22,
    USER3_ID      = 0x23,
#if NUM_PICSTYLES > 9
    AUTO_ID       = 0x87,
#endif
#if NUM_PICSTYLES > 10
    FINEDETAIL_ID = 0x88
#endif
} picstyle_ID;

typedef enum
{
    // This is "in menu" order which is different than property order. Starts from 1
#if NUM_PICSTYLES > 9
    AUTO,
#endif
    STD,
    PORTRAIT,
    LANDSCAPE,
#if NUM_PICSTYLES > 10
    FINEDETAIL,
#endif
    NEUTRAL,
    FAITHFUL,
    MONO,
    USER1,
    USER2,
    USER3
} picstyle_index;


/*
 * Data structures for picture style properties
 */

#if NUM_PICSTYLE_SLIDERS == 4
struct picstyle_settings_prop_struct
{
        int32_t     contrast;   // -4..4
        uint32_t    sharpness;  // 0..7
        int32_t     saturation; // -4..4
        int32_t     color_tone; // -4..4
        uint32_t    off_0x10;   // 0xDEADBEEF, unused
        uint32_t    off_0x14;   // 0xDEADBEEF, unused
} __attribute__((aligned,packed));

SIZE_CHECK_STRUCT( picstyle_settings_prop_struct, 0x18 );
#elif NUM_PICSTYLE_SLIDERS == 6
struct picstyle_settings_prop_struct
{
        int32_t     contrast;   // -4..4
        uint32_t    sharpness;  // 0..7,
        int32_t     saturation; // -4..4
        int32_t     color_tone; // -4..4
        uint32_t    off_0x10;   // 0xDEADBEEF, unused
        uint32_t    off_0x14;   // 0xDEADBEEF, unused
        uint32_t    sharpness_fineness;   // 1..5
        uint32_t    sharpness_threshold;  // 1..5
} __attribute__((aligned,packed));

SIZE_CHECK_STRUCT( picstyle_settings_prop_struct, 0x20 );
#endif

struct picstyle_settings_prop_struct picstyle_settings[NUM_PICSTYLES];
uint32_t picstyle_settings_prop_sizes[NUM_PICSTYLES];

static char user_picstyle_name_1[50] = "";
static char user_picstyle_name_2[50] = "";
static char user_picstyle_name_3[50] = "";


/*
 * Utility functions to convert between picstyle_index, picstyle_id and prop_id
 */

// Converts picstyle ID to Canon menu position (index)
static picstyle_ID get_picstyle_ID(picstyle_index index)
{
    switch(index)
    {
        case STD:        return STD_ID;
        case PORTRAIT:   return PORTRAIT_ID;
        case LANDSCAPE:  return LANDSCAPE_ID;
        case NEUTRAL:    return NEUTRAL_ID;
        case FAITHFUL:   return FAITHFUL_ID;
        case MONO:       return MONO_ID;
        case USER1:      return USER1_ID;
        case USER2:      return USER2_ID;
        case USER3:      return USER3_ID;
#if NUM_PICSTYLES > 9
        case AUTO:       return AUTO_ID;
#endif
#if NUM_PICSTYLES > 10
        case FINEDETAIL: return FINEDETAIL_ID;
#endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "get_picstyle_ID() unk. index: %x", index);
    return STD_ID;  // Fallback to something safe
}

// Converts picstyle menu index to picstyle ID.
static picstyle_index get_prop_picstyle_index(picstyle_ID pic_style)
{
    switch(pic_style)
    {
        case STD_ID:        return STD;
        case PORTRAIT_ID:   return PORTRAIT;
        case LANDSCAPE_ID:  return LANDSCAPE;
        case NEUTRAL_ID:    return NEUTRAL;
        case FAITHFUL_ID:   return FAITHFUL;
        case MONO_ID:       return MONO;
        case USER1_ID:      return USER1;
        case USER2_ID:      return USER2;
        case USER3_ID:      return USER3;
#if NUM_PICSTYLES > 9
        case AUTO_ID:       return AUTO;
#endif
#if NUM_PICSTYLES > 10
        case FINEDETAIL_ID: return FINEDETAIL;
#endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "get_prop_picstyle_index() unk. id: %x", pic_style);
    return STD; // Fallback to something safe
}

// Converts canon menu position (index) to PropID
static uint32_t get_picstyle_prop_ID(picstyle_index index)
{
    switch(index)
    {
        case STD:        return PROP_PICSTYLE_SETTINGS_STANDARD;
        case PORTRAIT:   return PROP_PICSTYLE_SETTINGS_PORTRAIT;
        case LANDSCAPE:  return PROP_PICSTYLE_SETTINGS_LANDSCAPE;
        case NEUTRAL:    return PROP_PICSTYLE_SETTINGS_NEUTRAL;
        case FAITHFUL:   return PROP_PICSTYLE_SETTINGS_FAITHFUL;
        case MONO:       return PROP_PICSTYLE_SETTINGS_MONOCHROME;
        case USER1:      return PROP_PICSTYLE_SETTINGS_USERDEF1;
        case USER2:      return PROP_PICSTYLE_SETTINGS_USERDEF2;
        case USER3:      return PROP_PICSTYLE_SETTINGS_USERDEF3;
#if NUM_PICSTYLES > 9
        case AUTO:       return PROP_PICSTYLE_SETTINGS_AUTO;
#endif
#if NUM_PICSTYLES > 10
        case FINEDETAIL: return PROP_PICSTYLE_SETTINGS_FINEDETAIL;
#endif
    }
    return PROP_PICSTYLE_SETTINGS_STANDARD; // Fallback to something safe
}


/*
 * Property handlers
 */

// PROP_PICTURE_STYLE holds picstyle_id for active picstyle
// Canon calls it PROP_FLAVOR_MODE, keeping our old name for readability.
PROP_HANDLER(PROP_PICTURE_STYLE)
{
    const uint32_t raw = *(uint32_t *) buf;
    lens_info.raw_picstyle = raw;
    lens_info.picstyle = get_prop_picstyle_index(raw);
}

// PROP_PICSTYLE_SETTINGS_XYZ holds actual settings for picstyle XYZ
// Canon calls it PROP_FLAVOUR_XYZ, again keeping our names for readability
// TODO: That's a lot of code repetition, but it is macros...
//       can we somehow compact that?
// TODO: Implemented runtime property size checks (as those changed mid D6 gen)
//       This can be removed when general solution is implemented in property.c
PROP_HANDLER(PROP_PICSTYLE_SETTINGS_STANDARD)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[STD] = len;
    memcpy(&picstyle_settings[STD], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_PORTRAIT)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[PORTRAIT] = len;
    memcpy(&picstyle_settings[PORTRAIT], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_LANDSCAPE)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[LANDSCAPE] = len;
    memcpy(&picstyle_settings[LANDSCAPE], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_NEUTRAL)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[NEUTRAL] = len;
    memcpy(&picstyle_settings[NEUTRAL], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_FAITHFUL)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[FAITHFUL] = len;
    memcpy(&picstyle_settings[FAITHFUL], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_MONOCHROME)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[MONO] = len;
    memcpy(&picstyle_settings[MONO], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_USERDEF1)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[USER1] = len;
    memcpy(&picstyle_settings[USER1], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_USERDEF2)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[USER2] = len;
    memcpy(&picstyle_settings[USER2], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_USERDEF3)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[USER3] = len;
    memcpy(&picstyle_settings[USER3], buf, sizeof(*picstyle_settings));
}

#if NUM_PICSTYLES > 9 // Since late Digic 4

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_AUTO)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[AUTO] = len;
    memcpy(&picstyle_settings[AUTO], buf, sizeof(*picstyle_settings));
}

#endif

#if NUM_PICSTYLES > 10 // Since late Digic 6

PROP_HANDLER(PROP_PICSTYLE_SETTINGS_FINEDETAIL)
{
    if(len != sizeof(*picstyle_settings)) return;
    picstyle_settings_prop_sizes[FINEDETAIL] = len;
    memcpy(&picstyle_settings[FINEDETAIL], buf, sizeof(*picstyle_settings));
}

#endif

// PROP_PC_FLAVORx_PARAM contains data related to custom picstyles like
// Technicolor, etc.
// (kitor) I did not investigate it any further than confirming custom style
// name still works on new generations.
// TODO: Define a proper data structure for those properties
PROP_HANDLER(PROP_PC_FLAVOR1_PARAM)
{
    if(len != 50) return;
    snprintf(user_picstyle_name_1, 50, "%s", (char*) buf + 4);
}

PROP_HANDLER(PROP_PC_FLAVOR2_PARAM)
{
    if(len != 50) return;
    snprintf(user_picstyle_name_2, 50, "%s", (char*) buf + 4);
}

PROP_HANDLER(PROP_PC_FLAVOR3_PARAM)
{
    if(len != 50) return;
    snprintf(user_picstyle_name_3, 50, "%s", (char*) buf + 4);
}

// PROP_PICSTYLE_OF_USERDEFx keep a base picstyle ID assigned to user defined
// picture styles.
static PROP_INT(PROP_PICSTYLE_OF_USERDEF1, picstyle_of_user1);
static PROP_INT(PROP_PICSTYLE_OF_USERDEF2, picstyle_of_user2);
static PROP_INT(PROP_PICSTYLE_OF_USERDEF3, picstyle_of_user3);


/*
 * Utility functions to get picture style display name
 */

// Returns picstyle display name from picstyle ID
static const char* picstyle_get_name(picstyle_ID pic_style)
{
    switch(pic_style)
    {
        case STD_ID:        return "Standard";
        case PORTRAIT_ID:   return "Portrait";
        case LANDSCAPE_ID:  return "Landscape";
        case NEUTRAL_ID:    return "Neutral";
        case FAITHFUL_ID:   return "Faithful";
        case MONO_ID:       return "Monochrome";
        case USER1_ID:      return (picstyle_of_user1 < 0x80 ? user_picstyle_name_1 : "UserDef1");
        case USER2_ID:      return (picstyle_of_user2 < 0x80 ? user_picstyle_name_2 : "UserDef2");
        case USER3_ID:      return (picstyle_of_user3 < 0x80 ? user_picstyle_name_3 : "UserDef3");
#if NUM_PICSTYLES > 9
        case AUTO_ID:       return "Auto";
#endif
#if NUM_PICSTYLES > 10
        case FINEDETAIL_ID: return "Fine Detail";
#endif
    }
    return "Unknown";
}

// Returns picstyle display name for active picstyle
const char* picstyle_get_current_name()
{
    return picstyle_get_name(lens_info.raw_picstyle);
}


/*
 * Getters and setters for picture style parameters.
 * Macros generate getters and setters for "currently active picstyle":
 *  - picstyle_get_current_x()
 *  - picstyle_set_current_x()
 * and only getters for arbitrary picstyle ID:
 *  - picstyle_get_x(picstyle_ID)
 */

// get parameter from active picture style
#define PICSTYLE_GET_PARAM_FROM_CURRENT(param) \
int \
picstyle_get_current_##param() \
{ \
    int i = lens_info.picstyle; \
    if ((i < 0) || (i >= NUM_PICSTYLES)) return -10; \
    return picstyle_settings[i].param; \
} \

#define PICSTYLE_GET_PARAM_FROM_CURRENT_DUMMY(param) \
int \
picstyle_get_current_##param() \
{ \
    return -10; \
} \

PICSTYLE_GET_PARAM_FROM_CURRENT(contrast)
PICSTYLE_GET_PARAM_FROM_CURRENT(sharpness)
PICSTYLE_GET_PARAM_FROM_CURRENT(saturation)
PICSTYLE_GET_PARAM_FROM_CURRENT(color_tone)
#if NUM_PICSTYLE_SLIDERS == 6
PICSTYLE_GET_PARAM_FROM_CURRENT(sharpness_fineness)
PICSTYLE_GET_PARAM_FROM_CURRENT(sharpness_threshold)
#else
PICSTYLE_GET_PARAM_FROM_CURRENT_DUMMY(sharpness_fineness)
PICSTYLE_GET_PARAM_FROM_CURRENT_DUMMY(sharpness_threshold)
#endif

// set parameter in active picture style
#define PICSTYLE_SET_PARAM_IN_CURRENT(param,lo,hi) \
void \
picstyle_set_current_##param(int value) \
{ \
    if (value < lo || value > hi) return; \
    int i = lens_info.picstyle; \
    if ((i < 0) || (i >= NUM_PICSTYLES)) return; \
    picstyle_settings[i].param = value; \
    int prop_desired_size = picstyle_settings_prop_sizes[i]; \
    if((prop_desired_size == 0) || (prop_desired_size != sizeof(*picstyle_settings))) return; \
    prop_request_change(get_picstyle_prop_ID(i), &picstyle_settings[i], sizeof(*picstyle_settings)); \
} \

PICSTYLE_SET_PARAM_IN_CURRENT(contrast, -4, 4)
PICSTYLE_SET_PARAM_IN_CURRENT(sharpness, -1, 7)
PICSTYLE_SET_PARAM_IN_CURRENT(saturation, -4, 4)
PICSTYLE_SET_PARAM_IN_CURRENT(color_tone, -4, 4)
#if NUM_PICSTYLE_SLIDERS == 6
PICSTYLE_SET_PARAM_IN_CURRENT(sharpness_fineness, 1, 5)
PICSTYLE_SET_PARAM_IN_CURRENT(sharpness_threshold, 1, 5)
#endif

// get parameter from other picstyle by picstyle ID
#define PICSTYLE_GET_PARAM_FROM_OTHER(param) \
int \
picstyle_get_##param(picstyle_ID pic_style) \
{ \
    return picstyle_settings[pic_style].param; \
} \

// get parameter from other picstyle by picstyle ID - dummy function
// for backward compatibility
#define PICSTYLE_GET_PARAM_FROM_OTHER_DUMMY(param) \
static int \
picstyle_get_##param(picstyle_ID pic_style) \
{ \
    return -10; \
} \

PICSTYLE_GET_PARAM_FROM_OTHER(contrast)
PICSTYLE_GET_PARAM_FROM_OTHER(sharpness)
PICSTYLE_GET_PARAM_FROM_OTHER(saturation)
PICSTYLE_GET_PARAM_FROM_OTHER(color_tone)
#if NUM_PICSTYLE_SLIDERS == 6
PICSTYLE_GET_PARAM_FROM_OTHER(sharpness_fineness)
PICSTYLE_GET_PARAM_FROM_OTHER(sharpness_threshold)
#else
// Backwards compatibility.
PICSTYLE_GET_PARAM_FROM_OTHER_DUMMY(sharpness_fineness)
PICSTYLE_GET_PARAM_FROM_OTHER_DUMMY(sharpness_threshold)
#endif

// Data structures for FEATURE_PICSTYLE
static CONFIG_INT("picstyle.rec.enable", picstyle_rec_enable, 0);
static CONFIG_INT("picstyle.rec", picstyle_rec, 0);
static int picstyle_before_rec = 0; // if you use a custom picstyle during REC, the old one will be saved here

void picstyle_mvr_log_append(char* mvr_logfile_buffer){
    MVR_LOG_APPEND (
        "Picture Style  : %s (%d,%d,%d,%d,%d,%d)\n",
        picstyle_get_current_name(),
        picstyle_get_current_sharpness(),
        picstyle_get_current_sharpness_fineness(),
        picstyle_get_current_sharpness_threshold(),
        picstyle_get_current_contrast(),
        ABS(picstyle_get_current_saturation()) < 10 ? picstyle_get_current_saturation() : 0,
        ABS(picstyle_get_current_color_tone()) < 10 ? picstyle_get_current_color_tone() : 0
        );
}


/*
 * Magic Lanterns menu handlers for FEATURE_PICSTYLE / FEATURE_REC_PICSTYLE
 */

// Sharpness / Sharpness strength
static void sharpness_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_sharpness();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    picstyle_set_current_sharpness(newc);
}

static MENU_UPDATE_FUNC(sharpness_display)
{
    int s = picstyle_get_current_sharpness();
    MENU_SET_VALUE( "%d", s );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}

#if NUM_PICSTYLE_SLIDERS == 6

// Sharpness fineness
static void sharpness_fineness_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_sharpness_fineness();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    picstyle_set_current_sharpness_fineness(newc);
}

static MENU_UPDATE_FUNC(sharpness_fineness_display)
{
    int s = picstyle_get_current_sharpness_fineness();
    MENU_SET_VALUE( "%d", s );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}

// Sharpness threshold
static void sharpness_threshold_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_sharpness_threshold();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    picstyle_set_current_sharpness_threshold(newc);
}

static MENU_UPDATE_FUNC(sharpness_threshold_display)
{
    int s = picstyle_get_current_sharpness_threshold();
    MENU_SET_VALUE( "%d", s );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}

#endif // NUM_PICSTYLE_SLIDERS == 6

// Contrast
static void contrast_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_contrast();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    picstyle_set_current_contrast(newc);
}

static MENU_UPDATE_FUNC(contrast_display)
{
    int s = picstyle_get_current_contrast();
    MENU_SET_VALUE( "%d", s );
    MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
}

// Saturation
static void saturation_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_saturation();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    picstyle_set_current_saturation(newc);
}

static MENU_UPDATE_FUNC(saturation_display)
{
    int s = picstyle_get_current_saturation();
    int ok = (s >= -4 && s <= 4);
    MENU_SET_VALUE(ok ? "%d " : "N/A", s);
    MENU_SET_ENABLED(ok);
    if (ok) MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
    else { MENU_SET_ICON(MNI_OFF, 0); MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "N/A"); }
}

// Color tone
static void color_tone_toggle(void *priv, int sign)
{
    int c = picstyle_get_current_color_tone();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    picstyle_set_current_color_tone(newc);
}

static MENU_UPDATE_FUNC(color_tone_display)
{
    int s = picstyle_get_current_color_tone();
    int ok = (s >= -4 && s <= 4);
    MENU_SET_VALUE(ok ? "%d " : "N/A", s );
    MENU_SET_ENABLED(ok);
    if (ok) MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
    else { MENU_SET_ICON(MNI_OFF, 0); MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "N/A"); }
}


// Picture style submenu display & active picture style toggle
static void picstyle_toggle(void *priv, int sign)
{
    if (RECORDING) return;
    int p = lens_info.picstyle;
    p = MOD(p + sign, NUM_PICSTYLES);
    p = get_picstyle_ID(p);
    prop_request_change(PROP_PICTURE_STYLE, &p, 4);
}

static MENU_UPDATE_FUNC(picstyle_display)
{
    int i = picstyle_rec && RECORDING ? picstyle_before_rec : (int)lens_info.picstyle;

    MENU_SET_VALUE(picstyle_get_name(get_picstyle_ID(i)));

    if (picstyle_rec_enable && is_movie_mode())
    {
        MENU_SET_RINFO(
            "REC:%s",
            picstyle_get_name(get_picstyle_ID(picstyle_rec))
        );
    }
#if NUM_PICSTYLE_SLIDERS == 4
    else MENU_SET_RINFO(
            "%d,%d,%d,%d",
            picstyle_get_sharpness(i),
            picstyle_get_contrast(i),
            ABS(picstyle_get_saturation(i)) < 10 ? picstyle_get_saturation(i) : 0,
            ABS(picstyle_get_color_tone(i)) < 10 ? picstyle_get_color_tone(i) : 0
        );
#elif NUM_PICSTYLE_SLIDERS == 6
    else MENU_SET_RINFO(
            "%d,%d,%d,%d,%d,%d",
            picstyle_get_sharpness(i),
            picstyle_get_sharpness_fineness(i),
            picstyle_get_sharpness_threshold(i),
            picstyle_get_contrast(i),
            ABS(picstyle_get_saturation(i)) < 10 ? picstyle_get_saturation(i) : 0,
            ABS(picstyle_get_color_tone(i)) < 10 ? picstyle_get_color_tone(i) : 0
        );
#endif

    MENU_SET_ENABLED(1);
}

static MENU_UPDATE_FUNC(picstyle_display_submenu)
{
    int p = get_picstyle_ID(lens_info.picstyle);
    MENU_SET_VALUE("%s", picstyle_get_name(p));
    MENU_SET_ENABLED(1);
}



#ifdef FEATURE_REC_PICSTYLE

static MENU_UPDATE_FUNC(picstyle_rec_display)
{
    MENU_SET_VALUE(
        picstyle_get_name(get_picstyle_ID(picstyle_rec))
    );
    MENU_SET_RINFO(
#if NUM_PICSTYLE_SLIDERS == 6
        "%d,%d,%d,%d,%d,%d",
        picstyle_get_sharpness(picstyle_rec),
        picstyle_get_sharpness_fineness(picstyle_rec),
        picstyle_get_sharpness_threshold(picstyle_rec),
#else
        "%d,%d,%d,%d",
        picstyle_get_sharpness(picstyle_rec),
#endif // NUM_PICSTYLE_SLIDERS
        picstyle_get_contrast(picstyle_rec),
        ABS(picstyle_get_saturation(picstyle_rec)) < 10 ? picstyle_get_saturation(picstyle_rec) : 0,
        ABS(picstyle_get_color_tone(picstyle_rec)) < 10 ? picstyle_get_color_tone(picstyle_rec) : 0
    );
}

static void picstyle_rec_toggle(void *priv, int sign)
{
    if (RECORDING) return;
    picstyle_rec = MOD(picstyle_rec + sign, NUM_PICSTYLES);
}

void picstyle_change_for_rec(int rec)
{
    static int prev = 0;

    if (picstyle_rec_enable)
    {
        if (prev == 0 && rec) // will start recording
        {
            picstyle_before_rec = lens_info.picstyle;
            int p = get_picstyle_ID(picstyle_rec);
            if (p)
            {
                NotifyBox(2000, "Picture Style : %s", picstyle_get_name(p));
                prop_request_change(PROP_PICTURE_STYLE, &p, 4);
            }
        }
        else if (prev == 2 && rec == 0) // recording => will stop
        {
            int p = get_picstyle_ID(picstyle_before_rec);
            {
                NotifyBox(2000, "Picture Style : %s", picstyle_get_name(p));
                prop_request_change(PROP_PICTURE_STYLE, &p, 4);
            }
            picstyle_before_rec = 0;
        }
    }
    prev = rec;
}

#endif // FEATURE_REC_PICSTYLE

const char * style_choices[] = {
#if NUM_PICSTYLES > 9 // 600D, 5D3...
    "Auto",
#endif
    "Standard", "Portrait", "Landscape",
#if NUM_PICSTYLES == 11 // D8 and up
    "FineDetail",
#endif
    "Neutral", "Faithful", "Monochrome", "UserDef1", "UserDef2", "UserDef3"
};

static struct menu_entry picstyle_features_menu[] = {
    {
        .name = "Picture Style",
        .priv = &lens_info.picstyle,
        .min = 0,
        .max = NUM_PICSTYLES - 1,
        .choices = style_choices,
        .update     = picstyle_display,
        .select     = picstyle_toggle,
        .help = "Change current picture style.",
        .edit_mode = EM_SHOW_LIVEVIEW,
        .icon_type = IT_DICE,
        .submenu_width = 700,
        .children =  (struct menu_entry[]) {
            {
                .name = "Active style",
                .priv = &lens_info.picstyle,
                .min = 0,
                .max = NUM_PICSTYLES - 1,
                .choices = style_choices,
                .update = picstyle_display_submenu,
                .select = picstyle_toggle,
                .help = "Change current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
                .icon_type = IT_DICE,
            },
#if NUM_PICSTYLE_SLIDERS == 6
            {
                .name = "Sharpness strength",
                .update = sharpness_display,
                .select = sharpness_toggle,
                .help = "Adjust sharpness strength in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Sharpness fineness",
                .update = sharpness_fineness_display,
                .select = sharpness_fineness_toggle,
                .help = "Adjust sharpness fineness in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Sharpness threshold",
                .update = sharpness_threshold_display,
                .select = sharpness_threshold_toggle,
                .help = "Adjust sharpness threshold in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
#else // Same as first param above, just under a different name
            {
                .name = "Sharpness",
                .update = sharpness_display,
                .select = sharpness_toggle,
                .help = "Adjust sharpness in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
#endif
            {
                .name = "Contrast",
                .update = contrast_display,
                .select = contrast_toggle,
                .help = "Adjust contrast in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Saturation",
                .update = saturation_display,
                .select = saturation_toggle,
                .help = "Adjust saturation in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Color Tone",
                .update = color_tone_display,
                .select = color_tone_toggle,
                .help = "Adjust color tone in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
#ifdef FEATURE_REC_PICSTYLE
            {
                .name = "Force style for REC",
                .priv = &picstyle_rec_enable,
                .min = 0,
                .max = 1,
                .choices = CHOICES("OFF", "ON"),
                .help = "Enable to override active picture style while recording.",
                .icon_type = IT_BOOL,
                .depends_on = DEP_MOVIE_MODE,
            },
            {
                .name = "Style for recording",
                .priv = &picstyle_rec,
                .min = 0,
                .max = NUM_PICSTYLES - 1,
                .choices = style_choices,
                .update = picstyle_rec_display,
                .select = picstyle_rec_toggle,
                .help = "Select which PicStyle to use while recording.",
                .icon_type = IT_DICE,
                .depends_on = DEP_MOVIE_MODE,
            },
#endif // FEATURE_REC_PICSTYLE
            MENU_EOL
        },
    },
};

/*
 * LiveView overlay handlers for FEATURE_PICSTYLE
 */

static LVINFO_UPDATE_FUNC(picstyle_update)
{
    LVINFO_BUFFER(12);

    if (is_movie_mode())
    {
        /* picture style has no effect on raw video => don't display */
        if (raw_lv_is_enabled())
            return;
    }
    else
    {
        /* when shooting RAW photos, picture style only affects the preview => don't display */
        // TODO: This fails on D8+ due to prop value change.
        //       See lens.c around line 2850.
        //       To be fixed in future as pic_quality is a can of worms that
        //       require similar treatment to picstyle.
        int jpg = pic_quality & 0x10000;
        if (!jpg)
            return;
    }

    snprintf(buffer, sizeof(buffer), "%s", (char*)picstyle_get_current_name() );
}

static struct lvinfo_item info_item = {
    .name = "Pic.Style",
    .which_bar = LV_TOP_BAR_ONLY,
    .update = picstyle_update,
    .priority = -1,
    .preferred_position = 5,
};


/*
 * FEATURE_PICSTYLE init functions
 */

static void picstyle_features_init()
{
    menu_add("Expo", picstyle_features_menu, COUNT(picstyle_features_menu));
    lvinfo_add_item(&info_item);
}

INIT_FUNC(__FILE__, picstyle_features_init);
#endif // FEATURE_PICSTYLE
