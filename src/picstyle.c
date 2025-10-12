#include <dryos.h>
#include <lens.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <picstyle.h>

// Converts picstyle ID to Canon menu position (index)
picstyle_id get_picstyle_menu_id(picstyle_menu_index index)
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
picstyle_menu_index get_prop_picstyle_index(picstyle_id pic_style)
{
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
        case PICSTYLE_USER3_ID:      return PICSTYLE_USER2;
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
uint32_t get_picstyle_prop_id(picstyle_menu_index index)
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

static uint32_t picstyle_of_user1;
static uint32_t picstyle_of_user2;
static uint32_t picstyle_of_user3;
//static PROP_INT(PROP_PICSTYLE_OF_USERDEF1, picstyle_of_user1);
//static PROP_INT(PROP_PICSTYLE_OF_USERDEF2, picstyle_of_user2);
//static PROP_INT(PROP_PICSTYLE_OF_USERDEF3, picstyle_of_user3);

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
    if (!i) return -10; \
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
    if (!i) return; \
    picstyle_settings[i].param = value; \
    prop_request_change(get_picstyle_prop_id(i), &picstyle_settings[i], sizeof(*picstyle_settings)); \
} \

// TODO: Add Digic 6+ sharpness params
LENS_GET_FROM_PICSTYLE(contrast)
LENS_GET_FROM_PICSTYLE(sharpness)
LENS_GET_FROM_PICSTYLE(saturation)
LENS_GET_FROM_PICSTYLE(color_tone)

LENS_GET_FROM_OTHER_PICSTYLE(contrast)
LENS_GET_FROM_OTHER_PICSTYLE(sharpness)
LENS_GET_FROM_OTHER_PICSTYLE(saturation)
LENS_GET_FROM_OTHER_PICSTYLE(color_tone)

LENS_SET_IN_PICSTYLE(contrast, -4, 4)
LENS_SET_IN_PICSTYLE(sharpness, -1, 7)
LENS_SET_IN_PICSTYLE(saturation, -4, 4)
LENS_SET_IN_PICSTYLE(color_tone, -4, 4)
