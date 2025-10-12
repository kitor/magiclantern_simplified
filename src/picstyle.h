#ifndef _picstyle_h_
#define _picstyle_h_

typedef enum{
    // This is "in menu" order which is different than property order. Starts from 1
    PICSTYLE_DNE,  // Placeholder to fill index 0
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
} picstyle_menu_index;

typedef enum{
    PICSTYLE_STD_ID        = 0x81,
    PICSTYLE_PORTRAIT_ID   = 0x82,
    PICSTYLE_LANDSCAPE_ID  = 0x83,
    PICSTYLE_NEUTRAL_ID    = 0x84,
    PICSTYLE_FAITHFUL_ID   = 0x85,
    PICSTYLE_MONO_ID       = 0x86,
    PICSTYLE_USER1_ID      = 0x21,
    PICSTYLE_USER2_ID      = 0x22,
    PICSTYLE_USER3_ID      = 0x23,
    #if NUM_PICSTYLES > 9
    PICSTYLE_AUTO_ID       = 0x87,
    #endif
    #if NUM_PICSTYLES > 10
    PICSTYLE_FINEDETAIL_ID = 0x88
    #endif
} picstyle_id;

#ifdef CONFIG_DIGIC_45
struct prop_picstyle_settings
{
        int32_t         contrast;   // -4..4
        uint32_t        sharpness;  // 0..7
        int32_t         saturation; // -4..4
        int32_t         color_tone; // -4..4
        uint32_t        off_0x10;   // 0xDEADBEEF?!
        uint32_t        off_0x14;   // 0xDEADBEEF?!
} __attribute__((aligned,packed));

SIZE_CHECK_STRUCT( prop_picstyle_settings, 0x18 );
#elif defined(CONFIG_DIGIC_678X)
// Since DIGIC 6 sharpness is split into three values
struct prop_picstyle_settings
{
        int32_t         contrast;   // -4..4
        uint32_t        sharpness;  // 0..7, sharpness strength
        int32_t         saturation; // -4..4
        int32_t         color_tone; // -4..4
        uint32_t        off_0x10;   // 0xDEADBEEF
        uint32_t        off_0x14;   // 0xDEADBEEF
        uint32_t        fineness;   // 1..5, sharpness fineness
        uint32_t        threshold;  // 1..5, sharpness threshold
} __attribute__((aligned,packed));

SIZE_CHECK_STRUCT( prop_picstyle_settings, 0x20 );
#else
#error prop_picstyle_settings not validated for your digic generation
#endif



// TODO: Can we somehow move choices from shoot.c FEATURE_PICSTYLE
// to here?

picstyle_id get_picstyle_menu_id(picstyle_menu_index index);
picstyle_menu_index  get_prop_picstyle_index(picstyle_id pic_style);
uint32_t get_picstyle_prop_id(picstyle_menu_index index);

/* todo: move them to picstyle.c */
const char * get_picstyle_name(picstyle_id pic_style);
const char * get_picstyle_shortname(picstyle_id pic_style);

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

#endif
