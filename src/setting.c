#include "setting.h"

LONG WinError(__in_opt DWORD errorCode) {
    LPSTR buffer = NULL;

    errorCode = errorCode ? labs(errorCode) : GetLastError();
    if (!errorCode)
        return errorCode;

    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, errorCode,
                       0, (LPSTR)&buffer, 0, NULL) > 0) {
        SetLastError(0);
    } else {
        LOGERR0("FormatMessage error!\n");
    }

    if (buffer)
        LocalFree(buffer);

    return -labs(errorCode);
}
