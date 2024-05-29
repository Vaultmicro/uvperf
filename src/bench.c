#include "transfer.h"
#include "log.h"
#include "utils.h"
#include "bench.h"
#include "uvperf.h"

BOOL Bench_Open(__in PUVPERF_PARAM TestParams) {
    UCHAR altSetting;
    KUSB_HANDLE associatedHandle;
    UINT transferred;
    KLST_DEVINFO_HANDLE deviceInfo;

    LstK_MoveReset(TestParams->DeviceList);

    while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo)) {
        UINT userContext = (UINT)LibK_GetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK);
        if (userContext != TRUE)
            continue;

        if (!LibK_LoadDriverAPI(&K, deviceInfo->DriverID)) {
            WinError(0);
            LOG_WARNING("can not load driver api %s\n", GetDrvIdString(deviceInfo->DriverID));
            continue;
        }
        if (!TestParams->use_UsbK_Init) {
            TestParams->DeviceHandle =
                CreateFileA(deviceInfo->DevicePath, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, NULL);

            if (!TestParams->DeviceHandle || TestParams->DeviceHandle == INVALID_HANDLE_VALUE) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                LOG_WARNING("can not create device handle\n%s\n", deviceInfo->DevicePath);
                continue;
            }

            if (!K.Initialize(TestParams->DeviceHandle, &TestParams->InterfaceHandle)) {
                WinError(0);
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("can not initialize device\n%s\n", deviceInfo->DevicePath);
                continue;
            }
        } else {
            if (!K.Init(&TestParams->InterfaceHandle, deviceInfo)) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("can not open device\n%s\n", deviceInfo->DevicePath);
                continue;
            }
        }

        if (!K.GetDescriptor(TestParams->InterfaceHandle, USB_DESCRIPTOR_TYPE_DEVICE, 0, 0,
                             (PUCHAR)&TestParams->DeviceDescriptor,
                             sizeof(TestParams->DeviceDescriptor), &transferred)) {
            WinError(0);

            K.Free(TestParams->InterfaceHandle);
            TestParams->InterfaceHandle = NULL;

            if (!TestParams->use_UsbK_Init) {
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
            }

            LOG_WARNING("can not get device descriptor\n%s\n", deviceInfo->DevicePath);
            continue;
        }
        TestParams->vid = (int)TestParams->DeviceDescriptor.idVendor;
        TestParams->pid = (int)TestParams->DeviceDescriptor.idProduct;

    NextInterface:

        memset(&TestParams->InterfaceDescriptor, 0, sizeof(TestParams->InterfaceDescriptor));
        altSetting = 0;

        while (K.QueryInterfaceSettings(TestParams->InterfaceHandle, altSetting,
                                        &TestParams->InterfaceDescriptor)) {
            UCHAR pipeIndex = 0;
            int hasIsoEndpoints = 0;
            int hasZeroMaxPacketEndpoints = 0;

            memset(&TestParams->PipeInformation, 0, sizeof(TestParams->PipeInformation));
            while (K.QueryPipeEx(TestParams->InterfaceHandle, altSetting, pipeIndex,
                                 &TestParams->PipeInformation[pipeIndex])) {
                if (TestParams->PipeInformation[pipeIndex].PipeType == UsbdPipeTypeIsochronous)
                    hasIsoEndpoints++;

                if (!TestParams->PipeInformation[pipeIndex].MaximumPacketSize)
                    hasZeroMaxPacketEndpoints++;

                pipeIndex++;
            }

            if (pipeIndex > 0) {
                if (((TestParams->intf == -1) ||
                     (TestParams->intf == TestParams->InterfaceDescriptor.bInterfaceNumber)) &&
                    ((TestParams->altf == -1) ||
                     (TestParams->altf == TestParams->InterfaceDescriptor.bAlternateSetting))) {
                    if (TestParams->altf == -1 && hasIsoEndpoints && hasZeroMaxPacketEndpoints) {
                        LOG_MSG("skipping interface %02X:%02X. zero-length iso endpoints exist.\n",
                                TestParams->InterfaceDescriptor.bInterfaceNumber,
                                TestParams->InterfaceDescriptor.bAlternateSetting);
                    } else {
                        TestParams->intf = TestParams->InterfaceDescriptor.bInterfaceNumber;
                        TestParams->altf = TestParams->InterfaceDescriptor.bAlternateSetting;
                        TestParams->SelectedDeviceProfile = deviceInfo;

                        if (hasIsoEndpoints && TestParams->bufferCount == 1)
                            TestParams->bufferCount++;

                        TestParams->DefaultAltSetting = 0;
                        K.GetCurrentAlternateSetting(TestParams->InterfaceHandle,
                                                     &TestParams->DefaultAltSetting);
                        if (!K.SetCurrentAlternateSetting(
                                TestParams->InterfaceHandle,
                                TestParams->InterfaceDescriptor.bAlternateSetting)) {
                            LOG_ERROR("can not find alt interface %02X\n", TestParams->altf);
                            return FALSE;
                        }
                        return TRUE;
                    }
                }
            }

            altSetting++;
            memset(&TestParams->InterfaceDescriptor, 0, sizeof(TestParams->InterfaceDescriptor));
        }
        if (K.GetAssociatedInterface(TestParams->InterfaceHandle, 0, &associatedHandle)) {
            K.Free(TestParams->InterfaceHandle);
            TestParams->InterfaceHandle = associatedHandle;
            goto NextInterface;
        }

        K.Free(TestParams->InterfaceHandle);
        TestParams->InterfaceHandle = NULL;
    }

    LOG_ERROR("device doesn't have %02X interface and %02X alt interface\n", TestParams->intf,
              TestParams->altf);
    return FALSE;
}

BOOL Bench_Configure(__in KUSB_HANDLE handle, __in UVPERF_DEVICE_COMMAND command, __in UCHAR intf,
                     __inout PUVPERF_DEVICE_TRANSFER_TYPE testType) {
    UCHAR buffer[1];
    UINT transferred = 0;
    WINUSB_SETUP_PACKET Pkt;
    KUSB_SETUP_PACKET *defPkt = (KUSB_SETUP_PACKET *)&Pkt;

    memset(&Pkt, 0, sizeof(Pkt));
    defPkt->BmRequest.Dir = BMREQUEST_DIR_DEVICE_TO_HOST;
    defPkt->BmRequest.Type = BMREQUEST_TYPE_VENDOR;
    defPkt->Request = (UCHAR)command;
    defPkt->Value = (UCHAR)*testType;
    defPkt->Index = intf;
    defPkt->Length = 1;

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return WinError(ERROR_INVALID_HANDLE);
    }

    if (K.ControlTransfer(handle, Pkt, buffer, 1, &transferred, NULL)) {
        if (transferred)
            return TRUE;
    }

    LOGERR0("can not configure device\n");
    return WinError(0);
}
