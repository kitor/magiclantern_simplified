#include <dryos.h>
#include <lens.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <picstyle.h>

picstyle_index get_prop_picstyle_from_index(picstyle_type index)
{
    switch(index)
    {
        case PICSTYLE_STD:        return PICSTYLE_STD_INDEX;
        case PICSTYLE_PORTRAIT:   return PICSTYLE_PORTRAIT_INDEX;
        case PICSTYLE_LANDSCAPE:  return PICSTYLE_LANDSCAPE_INDEX;
        case PICSTYLE_NEUTRAL:    return PICSTYLE_NEUTRAL_INDEX;
        case PICSTYLE_FAITHFUL:   return PICSTYLE_FAITHFUL_INDEX;
        case PICSTYLE_MONO:       return PICSTYLE_MONO_INDEX;
        case PICSTYLE_USER1:      return PICSTYLE_USER1_INDEX;
        case PICSTYLE_USER2:      return PICSTYLE_USER2_INDEX;
        case PICSTYLE_USER3:      return PICSTYLE_USER3_INDEX;
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO:       return PICSTYLE_AUTO_INDEX;
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL: return PICSTYLE_FINEDETAIL_INDEX;
        #endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "unk picstyle index: %x", index);
    return 0;
}

picstyle_type get_prop_picstyle_index(picstyle_index pic_style)
{
    switch(pic_style)
    {
        case PICSTYLE_STD_INDEX:        return PICSTYLE_STD;
        case PICSTYLE_PORTRAIT_INDEX:   return PICSTYLE_PORTRAIT;
        case PICSTYLE_LANDSCAPE_INDEX:  return PICSTYLE_LANDSCAPE;
        case PICSTYLE_NEUTRAL_INDEX:    return PICSTYLE_NEUTRAL;
        case PICSTYLE_FAITHFUL_INDEX:   return PICSTYLE_FAITHFUL;
        case PICSTYLE_MONO_INDEX:       return PICSTYLE_MONO;
        case PICSTYLE_USER1_INDEX:      return PICSTYLE_USER1;
        case PICSTYLE_USER2_INDEX:      return PICSTYLE_USER2;
        case PICSTYLE_USER3_INDEX:      return PICSTYLE_USER2;
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_INDEX:       return PICSTYLE_AUTO;
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_INDEX: return PICSTYLE_FINEDETAIL;
        #endif
    }
    bmp_printf(FONT_LARGE, 0, 0, "unk picstyle: %x", pic_style);
    return 0;
}

PROP_HANDLER(PROP_PICTURE_STYLE)
{
    const uint32_t raw = *(uint32_t *) buf;
    lens_info.raw_picstyle = raw;
    lens_info.picstyle = get_prop_picstyle_index(raw);
}

// TODO: Why do we define +1 ?
struct prop_picstyle_settings picstyle_settings[NUM_PICSTYLES + 1];

// prop_register_slave is much more difficult to use than copy/paste...

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_STANDARD ) {
    memcpy(&picstyle_settings[2], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_PORTRAIT ) {
    memcpy(&picstyle_settings[3], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_LANDSCAPE ) {
    memcpy(&picstyle_settings[4], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_NEUTRAL ) {
    memcpy(&picstyle_settings[5], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_FAITHFUL ) {
    memcpy(&picstyle_settings[6], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_MONOCHROME ) {
    memcpy(&picstyle_settings[7], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF1 ) {
    memcpy(&picstyle_settings[8], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF2 ) {
    memcpy(&picstyle_settings[9], buf, sizeof(*picstyle_settings));
}

PROP_HANDLER( PROP_PICSTYLE_SETTINGS_USERDEF3 ) {
    memcpy(&picstyle_settings[10], buf, sizeof(*picstyle_settings));
}

#if NUM_PICSTYLES > 9
PROP_HANDLER( PROP_PICSTYLE_SETTINGS_AUTO ) {
    memcpy(&picstyle_settings[10], buf, sizeof(*picstyle_settings));
}
#endif

#if NUM_PICSTYLES > 10
PROP_HANDLER( PROP_PICSTYLE_SETTINGS_FINEDETAIL ) {
    memcpy(&picstyle_settings[10], buf, sizeof(*picstyle_settings));
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

const char* get_picstyle_name(picstyle_index pic_style)
{
    switch(pic_style){
        case PICSTYLE_STD_INDEX:        return "Standard";
        case PICSTYLE_PORTRAIT_INDEX:   return "Portrait";
        case PICSTYLE_LANDSCAPE_INDEX:  return "Landscape";
        case PICSTYLE_NEUTRAL_INDEX:    return "Neutral";
        case PICSTYLE_FAITHFUL_INDEX:   return "Faithful";
        case PICSTYLE_MONO_INDEX:       return "Monochrome";
        case PICSTYLE_USER1_INDEX:      return (picstyle_of_user1 < 0x80 ? user_picstyle_name_1 : "UserDef1");
        case PICSTYLE_USER2_INDEX:      return (picstyle_of_user2 < 0x80 ? user_picstyle_name_2 : "UserDef2");
        case PICSTYLE_USER3_INDEX:      return (picstyle_of_user3 < 0x80 ? user_picstyle_name_3 : "UserDef3");
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_INDEX:       return "Auto";
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_INDEX: return "Fine Detail";
        #endif
    }
    return "Unknown";
}

const char* get_picstyle_shortname(picstyle_index pic_style)
{
    switch(pic_style){
        case PICSTYLE_STD_INDEX:        return "Std.";
        case PICSTYLE_PORTRAIT_INDEX:   return "Port.";
        case PICSTYLE_LANDSCAPE_INDEX:  return "Land.";
        case PICSTYLE_NEUTRAL_INDEX:    return "Neut.";
        case PICSTYLE_FAITHFUL_INDEX:   return "Fait.";
        case PICSTYLE_MONO_INDEX:       return "Mono.";
        case PICSTYLE_USER1_INDEX:      return (picstyle_of_user1 < 0x80 ? user_picstyle_shortname_1 : "User1");
        case PICSTYLE_USER2_INDEX:      return (picstyle_of_user2 < 0x80 ? user_picstyle_shortname_2 : "User2");
        case PICSTYLE_USER3_INDEX:      return (picstyle_of_user3 < 0x80 ? user_picstyle_shortname_3 : "User3");
        #if NUM_PICSTYLES > 9
        case PICSTYLE_AUTO_INDEX:       return "Auto";
        #endif
        #if NUM_PICSTYLES > 10
        case PICSTYLE_FINEDETAIL_INDEX: return "FinD.";
        #endif
    }
    return "Unk.";
}
