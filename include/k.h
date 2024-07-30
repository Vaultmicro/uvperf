#ifndef K_H
#define K_H

#include <windows.h>
#include "libusbk.h"

#define ENDPOINT_TYPE(TransferParam) (TransferParam->Ep.PipeType & 3)

extern KUSB_DRIVER_API K;
extern CRITICAL_SECTION DisplayCriticalSection;

extern const char *TestDisplayString[];
extern const char *EndpointTypeDisplayString[];

#endif // K_H
