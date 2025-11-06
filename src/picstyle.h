#ifndef _picstyle_h_
#define _picstyle_h_

#ifdef FEATURE_PICSTYLE

#if NUM_PICSTYLES > 10
#define NUM_PICSTYLE_SLIDERS 6
#else
#define NUM_PICSTYLE_SLIDERS 4
#endif

// Setters for params in active picture style
// Available only in "core" ML code
void picstyle_set_current_sharpness(int value);
void picstyle_set_current_contrast(int value);
void picstyle_set_current_saturation(int value);
void picstyle_set_current_color_tone(int value);
#if NUM_PICSTYLE_SLIDERS == 6
void picstyle_set_current_sharpness_fineness(int value);
void picstyle_set_current_sharpness_threshold(int value);
#endif

// Used by lens.c in mvr_create_logfile
void picstyle_mvr_log_append(char* mvr_logfile_buffer);

#ifdef FEATURE_REC_PICSTYLE
void picstyle_change_for_rec(int rec);
#endif // FEATURE_REC_PICSTYLE
#endif // FEATURE_PICSTYLE


#if defined(FEATURE_PICSTYLE) || defined(MODULE)
// Getters for params in active picture style
// Those are available for modules too.

const char * picstyle_get_current_name(void);
int picstyle_get_current_sharpness(void);
int picstyle_get_current_contrast(void);
int picstyle_get_current_saturation(void);
int picstyle_get_current_color_tone(void);
int picstyle_get_current_sharpness_fineness(void);
int picstyle_get_current_sharpnesst_threshold(void);
#endif // defined(FEATURE_PICSTYLE) || defined(MODULE)

#endif // _picstyle_h_
