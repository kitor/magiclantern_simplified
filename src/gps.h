#ifndef __GPS_H_
#define __GPS_H_

uint32_t gps_get_state();
#define GPS_OFF 0
#define GPS_EXTERNAL 1
#define GPS_INTERNAL 2

void gps_disable();
void gps_re_enable();

#endif // __GPS_H_
