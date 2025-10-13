#include <dryos.h>
#include <lens.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <picstyle.h>
#include <lvinfo.h>
#include <raw.h>

// Converts picstyle ID to Canon menu position (index)
picstyle_id get_picstyle_id(picstyle_index index)
{
    switch(index)
    {
        case PICSTYLE_STD:        return PICSTYLE_STD_ID;
        case PICSTYLE_PORTRAIT:   return PICSTYLE_PORTRAIT_ID;
        case PICSTYLE_LANDSCAPE:  return PICSTYLE_LANDSCAPE_ID;
        case PICSTYLE_NEUTRAL:    return PICSTYLE_NEUTRAL_ID;
        case PICSTYLE_FAITHFUL:   return PICSTYLE_FAITHFUL_ID;
        case PICSTYLE_MONO:       return PICSTYLE_MONO_ID;
        case PICSTYLE_USER1:      return PICSTYLE_USER1_ID;
        case PICSTYLE_USER2:      return PICSTYLE_USER2_ID;
        case PICSTYLE_USER3:      return PICSTYLE_USER3_ID;
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO:       return PICSTYLE_AUTO_ID;
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL: return PICSTYLE_FINEDETAIL_ID;
        #endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "unk picstyle index: %x", index);
    return 0;
}

// Converts picstyle menu index to picstyle ID.
picstyle_index get_prop_picstyle_index(picstyle_id pic_style)
{
    DryosDebugMsg(0, 15, "ajdi %08x", pic_style);
    switch(pic_style)
    {
        case PICSTYLE_STD_ID:        return PICSTYLE_STD;
        case PICSTYLE_PORTRAIT_ID:   return PICSTYLE_PORTRAIT;
        case PICSTYLE_LANDSCAPE_ID:  return PICSTYLE_LANDSCAPE;
        case PICSTYLE_NEUTRAL_ID:    return PICSTYLE_NEUTRAL;
        case PICSTYLE_FAITHFUL_ID:   return PICSTYLE_FAITHFUL;
        case PICSTYLE_MONO_ID:       return PICSTYLE_MONO;
        case PICSTYLE_USER1_ID:      return PICSTYLE_USER1;
        case PICSTYLE_USER2_ID:      return PICSTYLE_USER2;
        case PICSTYLE_USER3_ID:      return PICSTYLE_USER3;
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_ID:       return PICSTYLE_AUTO;
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_ID: return PICSTYLE_FINEDETAIL;
        #endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "unk picstyle: %x", pic_style);
    return 0;
}

// Converts canon menu position (index) to PropID
uint32_t get_picstyle_prop_id(picstyle_index index)
{
    switch(index)
    {
        case PICSTYLE_STD:        return PROP_PICSTYLE_SETTINGS_STANDARD;
        case PICSTYLE_PORTRAIT:   return PROP_PICSTYLE_SETTINGS_PORTRAIT;
        case PICSTYLE_LANDSCAPE:  return PROP_PICSTYLE_SETTINGS_LANDSCAPE;
        case PICSTYLE_NEUTRAL:    return PROP_PICSTYLE_SETTINGS_NEUTRAL;
        case PICSTYLE_FAITHFUL:   return PROP_PICSTYLE_SETTINGS_FAITHFUL;
        case PICSTYLE_MONO:       return PROP_PICSTYLE_SETTINGS_MONOCHROME;
        case PICSTYLE_USER1:      return PROP_PICSTYLE_SETTINGS_USERDEF1;
        case PICSTYLE_USER2:      return PROP_PICSTYLE_SETTINGS_USERDEF2;
        case PICSTYLE_USER3:      return PROP_PICSTYLE_SETTINGS_USERDEF3;
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO:       return PROP_PICSTYLE_SETTINGS_AUTO;
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL: return PROP_PICSTYLE_SETTINGS_FINEDETAIL;
        #endif
    }
    return PROP_PICSTYLE_SETTINGS_STANDARD; // Fallback to something safe
}

PROP_HANDLER(PROP_PICTURE_STYLE)
{
    const uint32_t raw = *(uint32_t *) buf;
    lens_info.raw_picstyle = raw;
    lens_info.picstyle = get_prop_picstyle_index(raw);
}

// N+1 for convinience - we use in-menu index for offsets (it starts from 1)
struct prop_picstyle_settings picstyle_settings[NUM_PICSTYLES + 1];

// prop_register_slave is much more difficult to use than copy/paste...

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_STANDARD ) {
    memcpy(&picstyle_settings[PICSTYLE_STD], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_PORTRAIT ) {
    memcpy(&picstyle_settings[PICSTYLE_PORTRAIT], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_LANDSCAPE ) {
    memcpy(&picstyle_settings[PICSTYLE_LANDSCAPE], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_NEUTRAL ) {
    memcpy(&picstyle_settings[PICSTYLE_NEUTRAL], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_FAITHFUL ) {
    memcpy(&picstyle_settings[PICSTYLE_FAITHFUL], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_MONOCHROME ) {
    memcpy(&picstyle_settings[PICSTYLE_MONO], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF1 ) {
    memcpy(&picstyle_settings[PICSTYLE_USER1], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF2 ) {
    memcpy(&picstyle_settings[PICSTYLE_USER2], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF3 ) {
    memcpy(&picstyle_settings[PICSTYLE_USER3], buf, sizeof(*picstyle_settings));
}

#if NUM_PICSTYLES > 9
PROP_HANDLER( PROP_PICSTYLE_SETTINGS_AUTO ) {
    memcpy(&picstyle_settings[PICSTYLE_AUTO], buf, sizeof(*picstyle_settings));
}
#endif

#if NUM_PICSTYLES > 10
PROP_HANDLER( PROP_PICSTYLE_SETTINGS_FINEDETAIL ) {
    memcpy(&picstyle_settings[PICSTYLE_FINEDETAIL], buf, sizeof(*picstyle_settings));
}
#endif

// Moved from shoot.c
static char user_picstyle_name_1[50] = "";
static char user_picstyle_name_2[50] = "";
static char user_picstyle_name_3[50] = "";
static char user_picstyle_shortname_1[10] = "";
static char user_picstyle_shortname_2[10] = "";
static char user_picstyle_shortname_3[10] = "";

static void copy_picstyle_name(char* fullname, char* shortname, char* name)
{
    snprintf(fullname, 50, "%s", name);
    // CineStyle => CineS
    // Flaat_10p => Fl10p
    // Flaat_2   => Flaa2
    // Flaat03   => Fla03

    int L = strlen(name);
    shortname[0] = name[0];
    shortname[1] = name[1];
    shortname[2] = name[2];
    shortname[3] = name[3];
    shortname[4] = name[4];
    shortname[5] = '\0';

    if (isdigit(name[L-3]))
        shortname[2] = name[L-3];
    if (isdigit(name[L-3]) || isdigit(name[L-2]))
        shortname[3] = name[L-2];
    if (isdigit(name[L-3]) || isdigit(name[L-2]) || isdigit(name[L-1]))
        shortname[4] = name[L-1];
}

PROP_HANDLER(PROP_PC_FLAVOR1_PARAM)
{
    copy_picstyle_name(user_picstyle_name_1, user_picstyle_shortname_1, (char*) buf + 4);
}

PROP_HANDLER(PROP_PC_FLAVOR2_PARAM)
{
    copy_picstyle_name(user_picstyle_name_2, user_picstyle_shortname_2, (char*) buf + 4);
}

PROP_HANDLER(PROP_PC_FLAVOR3_PARAM)
{
    copy_picstyle_name(user_picstyle_name_3, user_picstyle_shortname_3, (char*) buf + 4);
}

static PROP_INT(PROP_PICSTYLE_OF_USERDEF1, picstyle_of_user1);
static PROP_INT(PROP_PICSTYLE_OF_USERDEF2, picstyle_of_user2);
static PROP_INT(PROP_PICSTYLE_OF_USERDEF3, picstyle_of_user3);

const char* get_picstyle_name(picstyle_id pic_style)
{
    switch(pic_style){
        case PICSTYLE_STD_ID:        return "Standard";
        case PICSTYLE_PORTRAIT_ID:   return "Portrait";
        case PICSTYLE_LANDSCAPE_ID:  return "Landscape";
        case PICSTYLE_NEUTRAL_ID:    return "Neutral";
        case PICSTYLE_FAITHFUL_ID:   return "Faithful";
        case PICSTYLE_MONO_ID:       return "Monochrome";
        case PICSTYLE_USER1_ID:      return (picstyle_of_user1 < 0x80 ? user_picstyle_name_1 : "UserDef1");
        case PICSTYLE_USER2_ID:      return (picstyle_of_user2 < 0x80 ? user_picstyle_name_2 : "UserDef2");
        case PICSTYLE_USER3_ID:      return (picstyle_of_user3 < 0x80 ? user_picstyle_name_3 : "UserDef3");
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_ID:       return "Auto";
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_ID: return "Fine Detail";
        #endif
    }
    return "Unknown";
}

const char* get_picstyle_shortname(picstyle_id pic_style)
{
    switch(pic_style){
        case PICSTYLE_STD_ID:        return "Std.";
        case PICSTYLE_PORTRAIT_ID:   return "Port.";
        case PICSTYLE_LANDSCAPE_ID:  return "Land.";
        case PICSTYLE_NEUTRAL_ID:    return "Neut.";
        case PICSTYLE_FAITHFUL_ID:   return "Fait.";
        case PICSTYLE_MONO_ID:       return "Mono.";
        case PICSTYLE_USER1_ID:      return (picstyle_of_user1 < 0x80 ? user_picstyle_shortname_1 : "User1");
        case PICSTYLE_USER2_ID:      return (picstyle_of_user2 < 0x80 ? user_picstyle_shortname_2 : "User2");
        case PICSTYLE_USER3_ID:      return (picstyle_of_user3 < 0x80 ? user_picstyle_shortname_3 : "User3");
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_ID:       return "Auto";
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_ID: return "FinD.";
        #endif
    }
    return "Unk.";
}

// moved from lens.c

// get contrast/saturation/etc from the current picture style
#define LENS_GET_FROM_PICSTYLE(param) \
int \
lens_get_##param() \
{ \
    int i = lens_info.picstyle; \
    if ((i < 0) || (i >= NUM_PICSTYLES)) return -10; \
    return picstyle_settings[i].param; \
} \

#define LENS_GET_FROM_OTHER_PICSTYLE(param) \
int \
lens_get_from_other_picstyle_##param(int picstyle_index) \
{ \
    return picstyle_settings[picstyle_index].param; \
} \

// set contrast/saturation/etc in the current picture style (change is permanent!)
#define LENS_SET_IN_PICSTYLE(param,lo,hi) \
void \
lens_set_##param(int value) \
{ \
    if (value < lo || value > hi) return; \
    int i = lens_info.picstyle; \
    if ((i < 0) || (i >= NUM_PICSTYLES)) return; \
    picstyle_settings[i].param = value; \
    prop_request_change(get_picstyle_prop_id(i), &picstyle_settings[i], sizeof(*picstyle_settings)); \
} \

LENS_GET_FROM_PICSTYLE(contrast)
LENS_GET_FROM_PICSTYLE(sharpness)
LENS_GET_FROM_PICSTYLE(saturation)
LENS_GET_FROM_PICSTYLE(color_tone)
#ifdef CONFIG_DIGIC_678X
LENS_GET_FROM_PICSTYLE(sharpness_fineness)
LENS_GET_FROM_PICSTYLE(sharpness_threshold)
#endif

LENS_GET_FROM_OTHER_PICSTYLE(contrast)
LENS_GET_FROM_OTHER_PICSTYLE(sharpness)
LENS_GET_FROM_OTHER_PICSTYLE(saturation)
LENS_GET_FROM_OTHER_PICSTYLE(color_tone)
#ifdef CONFIG_DIGIC_678X
LENS_GET_FROM_OTHER_PICSTYLE(sharpness_fineness)
LENS_GET_FROM_OTHER_PICSTYLE(sharpness_threshold)
#endif

LENS_SET_IN_PICSTYLE(contrast, -4, 4)
LENS_SET_IN_PICSTYLE(sharpness, -1, 7)
LENS_SET_IN_PICSTYLE(saturation, -4, 4)
LENS_SET_IN_PICSTYLE(color_tone, -4, 4)
#ifdef CONFIG_DIGIC_678X
LENS_SET_IN_PICSTYLE(sharpness_fineness, 1, 5)
LENS_SET_IN_PICSTYLE(sharpness_threshold, 1, 5)
#endif



#ifdef FEATURE_PICSTYLE

// moved from lens.c
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
        int jpg = pic_quality & 0x10000;
        if (!jpg)
            return;
    }

    snprintf(buffer, sizeof(buffer), "%s",
        (char*)get_picstyle_name(lens_info.raw_picstyle)
    );
}

// moved from shoot.c

static void
contrast_toggle( void * priv, int sign )
{
    int c = lens_get_contrast();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    lens_set_contrast(newc);
}

static MENU_UPDATE_FUNC(contrast_display)
{
    int s = lens_get_contrast();
    MENU_SET_VALUE(
        "%d",
        s
    );
    MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
}

static void
sharpness_toggle( void * priv, int sign )
{
    int c = lens_get_sharpness();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    lens_set_sharpness(newc);
}

static MENU_UPDATE_FUNC(sharpness_display)
{
    int s = lens_get_sharpness();
    MENU_SET_VALUE(
        "%d ",
        s
    );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}

#ifdef CONFIG_DIGIC_678X
static void
sharpness_fineness_toggle( void * priv, int sign )
{
    int c = lens_get_sharpness_fineness();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    lens_set_sharpness_fineness(newc);
}

static MENU_UPDATE_FUNC(sharpness_fineness_display)
{
    int s = lens_get_sharpness_fineness();
    MENU_SET_VALUE(
        "%d ",
        s
    );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}

static void
sharpness_threshold_toggle( void * priv, int sign )
{
    int c = lens_get_sharpness_threshold();
    if (c < 0 || c > 7) return;
    int newc = MOD(c + sign, 8);
    lens_set_sharpness_threshold(newc);
}

static MENU_UPDATE_FUNC(sharpness_threshold_display)
{
    int s = lens_get_sharpness_threshold();
    MENU_SET_VALUE(
        "%d ",
        s
    );
    MENU_SET_ICON(MNI_PERCENT, s * 100 / 7);
}
#endif

static void
saturation_toggle( void * priv, int sign )
{
    int c = lens_get_saturation();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    lens_set_saturation(newc);
}

static MENU_UPDATE_FUNC(saturation_display)
{
    int s = lens_get_saturation();
    int ok = (s >= -4 && s <= 4);
    MENU_SET_VALUE(
        ok ? 
            "%d " :
            "N/A",
        s
    );
    MENU_SET_ENABLED(ok);
    if (ok) MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
    else { MENU_SET_ICON(MNI_OFF, 0); MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "N/A"); }
}

static void
color_tone_toggle( void * priv, int sign )
{
    int c = lens_get_color_tone();
    if (c < -4 || c > 4) return;
    int newc = MOD((c + 4 + sign), 9) - 4;
    lens_set_color_tone(newc);
}

static MENU_UPDATE_FUNC(color_tone_display)
{
    int s = lens_get_color_tone();
    int ok = (s >= -4 && s <= 4);
    MENU_SET_VALUE(
        ok ?
            "%d " :
            "N/A",
        s
    );
    MENU_SET_ENABLED(ok);
    if (ok) MENU_SET_ICON(MNI_PERCENT, (s+4) * 100 / 8);
    else { MENU_SET_ICON(MNI_OFF, 0); MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "N/A"); }
}

static CONFIG_INT("picstyle.rec", picstyle_rec, 0);
static int picstyle_before_rec = 0; // if you use a custom picstyle during REC, the old one will be saved here

static MENU_UPDATE_FUNC(picstyle_display)
{
    int i = picstyle_rec && RECORDING ? picstyle_before_rec : (int)lens_info.picstyle;

    MENU_SET_VALUE(
        get_picstyle_name(get_picstyle_id(i))
    );


    if (picstyle_rec && is_movie_mode())
    {
        MENU_SET_RINFO(
            "REC:%s",
            get_picstyle_name(get_picstyle_id(picstyle_rec))
        );
    }
    #ifdef CONFIG_DIGIC_45
    else MENU_SET_RINFO(
            "%d,%d,%d,%d",
            lens_get_from_other_picstyle_sharpness(i),
            lens_get_from_other_picstyle_contrast(i),
            ABS(lens_get_from_other_picstyle_saturation(i)) < 10 ? lens_get_from_other_picstyle_saturation(i) : 0,
            ABS(lens_get_from_other_picstyle_color_tone(i)) < 10 ? lens_get_from_other_picstyle_color_tone(i) : 0
        );
    #elif defined(CONFIG_DIGIC_678X)
    else MENU_SET_RINFO(
            "%d,%d,%d,%d,%d,%d",
            lens_get_from_other_picstyle_sharpness(i),
            lens_get_from_other_picstyle_sharpness_fineness(i),
            lens_get_from_other_picstyle_sharpness_threshold(i),
            lens_get_from_other_picstyle_contrast(i),
            ABS(lens_get_from_other_picstyle_saturation(i)) < 10 ? lens_get_from_other_picstyle_saturation(i) : 0,
            ABS(lens_get_from_other_picstyle_color_tone(i)) < 10 ? lens_get_from_other_picstyle_color_tone(i) : 0
        );
    #endif

    MENU_SET_ENABLED(1);
}

static MENU_UPDATE_FUNC(picstyle_display_submenu)
{
    int p = get_picstyle_id(lens_info.picstyle);
    MENU_SET_VALUE(
        "%s",
        get_picstyle_name(p)
    );
    MENU_SET_ENABLED(1);
}

static void
picstyle_toggle(void* priv, int sign )
{
    if (RECORDING) return;
    int p = lens_info.picstyle;
    p = MOD(p + sign, NUM_PICSTYLES);
    p = get_picstyle_id(p);
    prop_request_change(PROP_PICTURE_STYLE, &p, 4);
}

#ifdef FEATURE_REC_PICSTYLE

static MENU_UPDATE_FUNC(picstyle_rec_sub_display)
{
    if (!picstyle_rec)
    {
        MENU_SET_VALUE("OFF");
        return;
    }

    MENU_SET_VALUE(
        get_picstyle_name(get_picstyle_id(picstyle_rec))
    );
    //~ MENU_SET_RINFO(
    if (info->can_custom_draw) bmp_printf(MENU_FONT_GRAY, info->x_val, info->y + font_large.height,
        "%d,%d,%d,%d",
        lens_get_from_other_picstyle_sharpness(picstyle_rec),
        lens_get_from_other_picstyle_contrast(picstyle_rec),
        ABS(lens_get_from_other_picstyle_saturation(picstyle_rec)) < 10 ? lens_get_from_other_picstyle_saturation(picstyle_rec) : 0,
        ABS(lens_get_from_other_picstyle_color_tone(picstyle_rec)) < 10 ? lens_get_from_other_picstyle_color_tone(picstyle_rec) : 0
    );
}

static void
picstyle_rec_sub_toggle( void * priv, int delta )
{
    if (RECORDING) return;
    picstyle_rec = MOD(picstyle_rec+ delta, NUM_PICSTYLES+1);
}

void rec_picstyle_change(int rec)
{
    static int prev = 0;

    if (picstyle_rec)
    {
        if (prev == 0 && rec) // will start recording
        {
            picstyle_before_rec = lens_info.picstyle;
            int p = get_picstyle_prop_id(picstyle_rec);
            if (p)
            {
                NotifyBox(2000, "Picture Style : %s", get_picstyle_name(p));
                prop_request_change(PROP_PICTURE_STYLE, &p, 4);
            }
        }
        else if (prev == 2 && rec == 0) // recording => will stop
        {
            int p = get_picstyle_prop_id(picstyle_before_rec);
            if (p)
            {
                NotifyBox(2000, "Picture Style : %s", get_picstyle_name(p));
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
        .update     = picstyle_display,
        .select     = picstyle_toggle,
        .priv = &lens_info.picstyle,
        .help = "Change current picture style.",
        .edit_mode = EM_SHOW_LIVEVIEW,
        .icon_type = IT_DICE,
        .choices = style_choices,
        .min = 0,
        .max = NUM_PICSTYLES - 1,
        .submenu_width = 550,
        .submenu_height = 300,
        //~ .show_liveview = 1,
        //~ //.essential = FOR_PHOTO | FOR_MOVIE,
        .children =  (struct menu_entry[]) {
            {
                .name = "Picture Style",
                .priv = &lens_info.picstyle,
                .min = 1,
                .max = NUM_PICSTYLES,
                .choices = style_choices,
                .update     = picstyle_display_submenu,
                .select     = picstyle_toggle,
                .help = "Change current picture style.",
                //~ .show_liveview = 1,
                .edit_mode = EM_SHOW_LIVEVIEW,
                .icon_type = IT_DICE,
            },
            {
                .name = "Sharpness",
                .update     = sharpness_display,
                .select     = sharpness_toggle,
                .help = "Adjust sharpness in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            #ifdef CONFIG_DIGIC_678X
            {
                .name = "Sharpness fineness",
                .update     = sharpness_fineness_display,
                .select     = sharpness_fineness_toggle,
                .help = "Adjust sharpness fineness in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Sharpness threshold",
                .update     = sharpness_threshold_display,
                .select     = sharpness_threshold_toggle,
                .help = "Adjust sharpness threshold in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            #endif
            {
                .name = "Contrast",
                .update     = contrast_display,
                .select     = contrast_toggle,
                .help = "Adjust contrast in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Saturation",
                .update     = saturation_display,
                .select     = saturation_toggle,
                .help = "Adjust saturation in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
            {
                .name = "Color Tone",
                .update     = color_tone_display,
                .select     = color_tone_toggle,
                .help = "Adjust color tone in current picture style.",
                .edit_mode = EM_SHOW_LIVEVIEW,
            },
    #ifdef FEATURE_REC_PICSTYLE
            {
                .name = "REC-PicStyle",
                .priv = &picstyle_rec,
                .max  = NUM_PICSTYLES,
                .icon_type = IT_DICE_OFF,
                .update     = picstyle_rec_sub_display,
                .select     = picstyle_rec_sub_toggle,

                .choices = style_choices,
                .help = "You can use a different picture style when recording.",
                .depends_on = DEP_MOVIE_MODE,
            },
    #endif // FEATURE_REC_PICSTYLE
            MENU_EOL
        },
    },
};

static struct lvinfo_item info_item = {
    .name = "Pic.Style",
    .which_bar = LV_TOP_BAR_ONLY,
    .update = picstyle_update,
    .priority = -1,
    .preferred_position = 5,
};


static void picstyle_features_init()
{
    menu_add("Shoot", picstyle_features_menu, COUNT(picstyle_features_menu));
    lvinfo_add_item(&info_item);
}

INIT_FUNC(__FILE__, picstyle_features_init);
#endif // FEATURE_PICSTYLE
