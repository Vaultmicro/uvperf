#ifndef TIME_H
#define TIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void get_time(struct timespec *ts);

#ifdef __cplusplus
}
#endif

#endif /* TIME_H */