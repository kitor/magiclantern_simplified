#ifndef _picstyle_h_
#define _picstyle_h_

typedef enum{
    // This is "in menu" order which is different than property order
    #if NUM_PICSTYLES > 9
    PICSTYLE_AUTO,
    #endif
    PICSTYLE_STD,
    PICSTYLE_PORTRAIT,
    PICSTYLE_LANDSCAPE,
    #if NUM_PICSTYLES > 10
    //  With Digic 8 there's "Fine Detail" picture style added
    PICSTYLE_FINEDETAIL,
    #endif
    PICSTYLE_NEUTRAL,
    PICSTYLE_FAITHFUL,
    PICSTYLE_MONO,
    PICSTYLE_USER1,
    PICSTYLE_USER2,
    PICSTYLE_USER3
} picstyle_type;

typedef enum{
    PICSTYLE_STD_INDEX        = 0x81,
    PICSTYLE_PORTRAIT_INDEX   = 0x82,
    PICSTYLE_LANDSCAPE_INDEX  = 0x83,
    PICSTYLE_NEUTRAL_INDEX    = 0x84,
    PICSTYLE_FAITHFUL_INDEX   = 0x85,
    PICSTYLE_MONO_INDEX       = 0x86,
    PICSTYLE_USER1_INDEX      = 0x21,
    PICSTYLE_USER2_INDEX      = 0x22,
    PICSTYLE_USER3_INDEX      = 0x23,
    #if NUM_PICSTYLES > 9
    PICSTYLE_AUTO_INDEX       = 0x87,
    #endif
    #if NUM_PICSTYLES > 10
    PICSTYLE_FINEDETAIL_INDEX = 0x88
    #endif
} picstyle_index;

// TODO: Can we somehow move choices from shoot.c FEATURE_PICSTYLE
// to here?

picstyle_index get_prop_picstyle_from_index(picstyle_type index);
picstyle_type  get_prop_picstyle_index(picstyle_index pic_style);

/* todo: move them to picstyle.c */
const char * get_picstyle_name(picstyle_index pic_style);
const char * get_picstyle_shortname(picstyle_index pic_style);

int lens_get_sharpness(void);
int lens_get_contrast(void);
int lens_get_saturation(void);
int lens_get_color_tone(void);

void lens_set_sharpness(int value);
void lens_set_contrast(int value);
void lens_set_saturation(int value);
void lens_set_color_tone(int value);

int lens_get_from_other_picstyle_sharpness(int index);
int lens_get_from_other_picstyle_contrast(int index);
int lens_get_from_other_picstyle_saturation(int index);
int lens_get_from_other_picstyle_color_tone(int index);

#define PROP_PICSTYLE_SETTINGS(i) (PROP_PICSTYLE_SETTINGS_STANDARD - 1 + i)

#endif
