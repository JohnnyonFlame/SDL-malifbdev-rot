/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_JOYSTICK_HIDAPI

#include "SDL_hints.h"
#include "SDL_events.h"
#include "SDL_timer.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
#include "../../SDL_hints_c.h"
#include "../SDL_sysjoystick.h"
#include "SDL_hidapijoystick_c.h"
#include "SDL_hidapi_rumble.h"


#ifdef SDL_JOYSTICK_HIDAPI_WII

/* Define this if you want to log all packets from the controller */
/*#define DEBUG_WII_PROTOCOL*/

typedef enum {
    k_eWiiInputReportIDs_Status      = 0x20,
    k_eWiiInputReportIDs_ReadMemory  = 0x21,
    k_eWiiInputReportIDs_Acknowledge = 0x22,
    k_eWiiInputReportIDs_ButtonData0 = 0x30,
    k_eWiiInputReportIDs_ButtonData1 = 0x31,
    k_eWiiInputReportIDs_ButtonData2 = 0x32,
    k_eWiiInputReportIDs_ButtonData3 = 0x33,
    k_eWiiInputReportIDs_ButtonData4 = 0x34,
    k_eWiiInputReportIDs_ButtonData5 = 0x35,
    k_eWiiInputReportIDs_ButtonData6 = 0x36,
    k_eWiiInputReportIDs_ButtonData7 = 0x37,
    k_eWiiInputReportIDs_ButtonDataD = 0x3D,
    k_eWiiInputReportIDs_ButtonDataE = 0x3E,
    k_eWiiInputReportIDs_ButtonDataF = 0x3F,
} EWiiInputReportIDs;

typedef enum {
    k_eWiiOutputReportIDs_Rumble            = 0x10,
    k_eWiiOutputReportIDs_LEDs              = 0x11,
    k_eWiiOutputReportIDs_DataReportingMode = 0x12,
    k_eWiiOutputReportIDs_IRCameraEnable    = 0x13,
    k_eWiiOutputReportIDs_SpeakerEnable     = 0x14,
    k_eWiiOutputReportIDs_StatusRequest     = 0x15,
    k_eWiiOutputReportIDs_WriteMemory       = 0x16,
    k_eWiiOutputReportIDs_ReadMemory        = 0x17,
    k_eWiiOutputReportIDs_SpeakerData       = 0x18,
    k_eWiiOutputReportIDs_SpeakerMute       = 0x19,
    k_eWiiOutputReportIDs_IRCameraEnable2   = 0x1a,
} EWiiOutputReportIDs;

typedef enum {
    k_eWiiPlayerLEDs_P1 = 0x10,
    k_eWiiPlayerLEDs_P2 = 0x20,
    k_eWiiPlayerLEDs_P3 = 0x40,
    k_eWiiPlayerLEDs_P4 = 0x80,
} EWiiPlayerLEDs;

typedef enum {
    k_eWiiExtensionControllerType_None,
    k_eWiiExtensionControllerType_Unknown,
    k_eWiiExtensionControllerType_Nunchuck,
    k_eWiiExtensionControllerType_ClassicController,
    k_eWiiExtensionControllerType_ClassicControllerPro,
    k_eWiiExtensionControllerType_WiiUPro,
} EWiiExtensionControllerType;

#define k_unWiiPacketDataLength 22

typedef struct {
    Uint8 rgucBaseButtons[2];
    Uint8 rgucAccelerometer[3];
    Uint8 rgucExtension[21];
    SDL_bool hasBaseButtons;
    SDL_bool hasAccelerometer;
    Uint8 ucNExtensionBytes;
} WiiButtonData;

typedef struct {
    SDL_HIDAPI_Device *device;
    EWiiExtensionControllerType m_eExtensionControllerType;
    SDL_bool m_bUseButtonLabels;
    SDL_bool m_bRumbleActive;
    Uint8 m_rgucReadBuffer[k_unWiiPacketDataLength];
    Uint32 m_iLastStatus;
    int m_playerIndex;

    struct StickCalibrationData {
        Uint16 min;
        Uint16 max;
        Uint16 center;
        Uint16 deadzone;
    } m_StickCalibrationData[6];
} SDL_DriverWii_Context;

static void
HIDAPI_DriverWii_RegisterHints(SDL_HintCallback callback, void *userdata)
{
    SDL_AddHintCallback(SDL_HINT_JOYSTICK_HIDAPI_WII, callback, userdata);
}

static void
HIDAPI_DriverWii_UnregisterHints(SDL_HintCallback callback, void *userdata)
{
    SDL_DelHintCallback(SDL_HINT_JOYSTICK_HIDAPI_WII, callback, userdata);
}

static SDL_bool
HIDAPI_DriverWii_IsEnabled(void)
{
    return SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_WII,
               SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI,
                   SDL_HIDAPI_DEFAULT));
}

static SDL_bool
HIDAPI_DriverWii_IsSupportedDevice(SDL_HIDAPI_Device *device, const char *name, SDL_GameControllerType type, Uint16 vendor_id, Uint16 product_id, Uint16 version, int interface_number, int interface_class, int interface_subclass, int interface_protocol)
{
    if (vendor_id == USB_VENDOR_NINTENDO && product_id == USB_PRODUCT_NINTENDO_WII_PRO) {
        return SDL_TRUE;
    }
    if (vendor_id == USB_VENDOR_NINTENDO && product_id == USB_PRODUCT_NINTENDO_WII_REMOTE) {
        /* Technically can be supported, but we don't interpret inputs yet */
        return SDL_FALSE;
    }
    return SDL_FALSE;
}

static const char *
HIDAPI_DriverWii_GetDeviceName(const char *name, Uint16 vendor_id, Uint16 product_id)
{
    return NULL;
}

static int ReadInput(SDL_DriverWii_Context *ctx)
{
    /* Make sure we don't try to read at the same time a write is happening */
    if (SDL_AtomicGet(&ctx->device->rumble_pending) > 0) {
        return 0;
    }

    return SDL_hid_read_timeout(ctx->device->dev, ctx->m_rgucReadBuffer, sizeof(ctx->m_rgucReadBuffer), 0);
}

static int WriteOutput(SDL_DriverWii_Context *ctx, const Uint8 *data, int size, SDL_bool sync)
{
    if (sync) {
        return SDL_hid_write(ctx->device->dev, data, size);
    } else {
        /* Use the rumble thread for general asynchronous writes */
        if (SDL_HIDAPI_LockRumble() < 0) {
            return -1;
        }
        return SDL_HIDAPI_SendRumbleAndUnlock(ctx->device, data, size);
    }
}

static SDL_bool ReadInputSync(SDL_DriverWii_Context *ctx, EWiiInputReportIDs expectedID, SDL_bool(*isMine)(const Uint8 *))
{
    Uint32 TimeoutMs = 100;
    Uint32 startTicks = SDL_GetTicks();

    int nRead = 0;
    while ((nRead = ReadInput(ctx)) != -1) {
        if (nRead > 0) {
            if (ctx->m_rgucReadBuffer[0] == expectedID && (!isMine || isMine(ctx->m_rgucReadBuffer))) {
                return SDL_TRUE;
            }
        } else {
            if (SDL_TICKS_PASSED(SDL_GetTicks(), startTicks + TimeoutMs)) {
                break;
            }
            SDL_Delay(1);
        }
    }
    SDL_SetError("Read timed out");
    return SDL_FALSE;
}

static SDL_bool IsWriteMemoryResponse(const Uint8 *data)
{
    return data[3] == k_eWiiOutputReportIDs_WriteMemory;
}

static SDL_bool WriteRegister(SDL_DriverWii_Context *ctx, Uint32 address, const Uint8 *data, int size, SDL_bool sync)
{
    Uint8 writeRequest[k_unWiiPacketDataLength] = {
        k_eWiiOutputReportIDs_WriteMemory,
        0x04 | ctx->m_bRumbleActive,
        (address >> 16) & 0xff,
        (address >> 8) & 0xff,
        address & 0xff,
        size
    };
    SDL_assert(size > 0 && size <= 16);
    SDL_memcpy(writeRequest + 6, data, size);

    if (!WriteOutput(ctx, writeRequest, sizeof(writeRequest), sync)) {
        return SDL_FALSE;
    }
    if (sync) {
        /* Wait for response */
        if (!ReadInputSync(ctx, k_eWiiInputReportIDs_Acknowledge, IsWriteMemoryResponse)) {
            return SDL_FALSE;
        }
        if (ctx->m_rgucReadBuffer[4]) {
            SDL_SetError("Write memory failed: %d", ctx->m_rgucReadBuffer[4]);
            return SDL_FALSE;
        }
    }
    return SDL_TRUE;
}

static SDL_bool ReadRegister(SDL_DriverWii_Context *ctx, Uint32 address, int size, SDL_bool sync)
{
    Uint8 readRequest[7] = {
        k_eWiiOutputReportIDs_ReadMemory,
        0x04 | ctx->m_bRumbleActive,
        (address >> 16) & 0xff,
        (address >> 8) & 0xff,
        address & 0xff,
        (size >> 8) & 0xff,
        size & 0xff
    };
    SDL_assert(size > 0 && size <= 0xffff);

    if (!WriteOutput(ctx, readRequest, sizeof(readRequest), sync)) {
        return SDL_FALSE;
    }
    if (sync) {
        SDL_assert(size <= 16); /* Only waiting for one packet is supported right now */
        /* Wait for response */
        if (!ReadInputSync(ctx, k_eWiiInputReportIDs_ReadMemory, NULL)) {
            return SDL_FALSE;
        }
    }
    return SDL_TRUE;
}

static SDL_bool ParseExtensionResponse(SDL_DriverWii_Context *ctx)
{
    Uint64 type = 0;
    SDL_assert(ctx->m_rgucReadBuffer[0] == k_eWiiInputReportIDs_ReadMemory);
    if (ctx->m_rgucReadBuffer[4] != 0x00 || ctx->m_rgucReadBuffer[5] != 0xFA) {
        SDL_SetError("Unexpected extension response address");
        return SDL_FALSE;
    }
    if (ctx->m_rgucReadBuffer[3] != 0x50) {
        if (ctx->m_rgucReadBuffer[3] & 0xF) {
            SDL_SetError("Failed to read extension type: %d", ctx->m_rgucReadBuffer[3] & 0xF);
        } else {
            SDL_SetError("Unexpected read length when reading extension type: %d", (ctx->m_rgucReadBuffer[3] >> 4) + 1);
        }
        return SDL_FALSE;
    }

    for (int i = 6; i < 12; i++) {
        type = type << 8 | ctx->m_rgucReadBuffer[i];
    }
    switch (type) {
        case 0x0000A4200000: ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_Nunchuck; break;
        case 0x0000A4200101: ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_ClassicController; break;
        case 0x0100A4200101: ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_ClassicControllerPro; break;
        case 0x0000A4200120: ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_WiiUPro; break;
        default:
            ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_Unknown;
            SDL_SetError("Unrecognized controller type: %012" SDL_PRIx64, type);
            return SDL_FALSE;
    }
    return SDL_TRUE;
}


static void UpdatePowerLevelWii(SDL_Joystick *joystick, Uint8 batteryLevelByte)
{
    if (batteryLevelByte > 178) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_FULL;
    } else if (batteryLevelByte > 51) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_MEDIUM;
    } else if (batteryLevelByte > 13) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_LOW;
    } else {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_EMPTY;
    }
}

static void UpdatePowerLevelWiiU(SDL_Joystick *joystick, Uint8 extensionBatteryByte)
{
    SDL_bool charging  = extensionBatteryByte & 0x08 ? SDL_FALSE : SDL_TRUE;
    SDL_bool pluggedIn = extensionBatteryByte & 0x04 ? SDL_FALSE : SDL_TRUE;
    Uint8 batteryLevel = extensionBatteryByte >> 4;

    /* Not sure if all Wii U Pro controllers act like this, but on mine
     * 4, 3, and 2 are held for about 20 hours each
     * 1 is held for about 6 hours
     * 0 is held for about 2 hours
     * No value above 4 has been observed.
     */
    if (pluggedIn && !charging) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_WIRED;
    } else if (batteryLevel >= 4) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_FULL;
    } else if (batteryLevel > 1) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_MEDIUM;
    } else if (batteryLevel == 1) {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_LOW;
    } else {
        joystick->epowerlevel = SDL_JOYSTICK_POWER_EMPTY;
    }
}

static SDL_bool IdentifyController(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick)
{
    static const Uint8 statusRequest[2] = { k_eWiiOutputReportIDs_StatusRequest, 0 };
    SDL_bool hasExtension;
    WriteOutput(ctx, statusRequest, sizeof(statusRequest), SDL_TRUE);
    if (!ReadInputSync(ctx, k_eWiiInputReportIDs_Status, NULL)) {
        return SDL_FALSE;
    }
    UpdatePowerLevelWii(joystick, ctx->m_rgucReadBuffer[6]);
    hasExtension = ctx->m_rgucReadBuffer[3] & 2 ? SDL_TRUE : SDL_FALSE;
    if (hasExtension) {
        /* http://wiibrew.org/wiki/Wiimote/Extension_Controllers#The_New_Way */
        SDL_bool ok = WriteRegister(ctx, 0xA400F0, (Uint8[1]){0x55}, 1, SDL_TRUE)
                   && WriteRegister(ctx, 0xA400FB, (Uint8[1]){0x00}, 1, SDL_TRUE)
                   && ReadRegister(ctx, 0xA400FA, 6, SDL_TRUE)
                   && ParseExtensionResponse(ctx);
        if (!ok) { return SDL_FALSE; }
    } else {
        ctx->m_eExtensionControllerType = k_eWiiExtensionControllerType_None;
    }
    return SDL_TRUE;
}

static EWiiInputReportIDs GetButtonPacketType(SDL_DriverWii_Context *ctx)
{
    switch (ctx->m_eExtensionControllerType) {
        case k_eWiiExtensionControllerType_WiiUPro:
            return k_eWiiInputReportIDs_ButtonDataD;
        case k_eWiiExtensionControllerType_Nunchuck:
        case k_eWiiExtensionControllerType_ClassicController:
        case k_eWiiExtensionControllerType_ClassicControllerPro:
            return k_eWiiInputReportIDs_ButtonData2;
        default:
            return k_eWiiInputReportIDs_ButtonData0;
    }
}

static SDL_bool RequestButtonPacketType(SDL_DriverWii_Context *ctx, EWiiInputReportIDs type)
{
    Uint8 tt = ctx->m_bRumbleActive;
    /* Continuous reporting off, tt & 4 == 0 */
    return WriteOutput(ctx, (Uint8[3]){ k_eWiiOutputReportIDs_DataReportingMode, tt, type }, 3, SDL_FALSE);
}

static void InitStickCalibrationData(SDL_DriverWii_Context *ctx)
{
    switch (ctx->m_eExtensionControllerType) {
        case k_eWiiExtensionControllerType_WiiUPro:
            for (int i = 0; i < 4; i++) {
                ctx->m_StickCalibrationData[i].min = 1000;
                ctx->m_StickCalibrationData[i].max = 3000;
                ctx->m_StickCalibrationData[i].center = 0;
                ctx->m_StickCalibrationData[i].deadzone = 100;
            }
            break;
        case k_eWiiExtensionControllerType_ClassicController:
        case k_eWiiExtensionControllerType_ClassicControllerPro:
            for (int i = 0; i < 4; i++) {
                ctx->m_StickCalibrationData[i].min = 0;
                ctx->m_StickCalibrationData[i].max = i < 2 ? 63 : 31;
                ctx->m_StickCalibrationData[i].center = 0;
                ctx->m_StickCalibrationData[i].deadzone = i < 2 ? 4 : 2;
            }
            break;
        case k_eWiiExtensionControllerType_Nunchuck:
            for (int i = 0; i < 2; i++) {
                ctx->m_StickCalibrationData[i].min = 0;
                ctx->m_StickCalibrationData[i].max = 255;
                ctx->m_StickCalibrationData[i].center = 0;
                ctx->m_StickCalibrationData[i].deadzone = 10;
            }
            break;
        default:
            break;
    }
}

static const char* GetNameFromExtensionInfo(SDL_DriverWii_Context *ctx)
{
    switch (ctx->m_eExtensionControllerType) {
        case k_eWiiExtensionControllerType_None:                 return "Nintendo Wii Remote";
        case k_eWiiExtensionControllerType_Nunchuck:             return "Nintendo Wii Remote with Nunchuck";
        case k_eWiiExtensionControllerType_ClassicController:    return "Nintendo Wii Remote with Classic Controller";
        case k_eWiiExtensionControllerType_ClassicControllerPro: return "Nintendo Wii Remote with Classic Controller Pro";
        case k_eWiiExtensionControllerType_WiiUPro:              return "Nintendo Wii U Pro Controller";
        default:                                                 return "Nintendo Wii Remote with Unknown Extension";
    }
}

static void SDLCALL SDL_GameControllerButtonReportingHintChanged(void *userdata, const char *name, const char *oldValue, const char *hint)
{
    SDL_DriverWii_Context *ctx = (SDL_DriverWii_Context *)userdata;
    ctx->m_bUseButtonLabels = SDL_GetStringBoolean(hint, SDL_TRUE);
}

static Uint8 RemapButton(SDL_DriverWii_Context *ctx, Uint8 button)
{
    if (!ctx->m_bUseButtonLabels) {
        switch (button) {
            case SDL_CONTROLLER_BUTTON_A:
                return SDL_CONTROLLER_BUTTON_B;
            case SDL_CONTROLLER_BUTTON_B:
                return SDL_CONTROLLER_BUTTON_A;
            case SDL_CONTROLLER_BUTTON_X:
                return SDL_CONTROLLER_BUTTON_Y;
            case SDL_CONTROLLER_BUTTON_Y:
                return SDL_CONTROLLER_BUTTON_X;
            default:
                break;
        }
    }
    return button;
}

static SDL_bool
HIDAPI_DriverWii_InitDevice(SDL_HIDAPI_Device *device)
{
    return HIDAPI_JoystickConnected(device, NULL);
}

static int
HIDAPI_DriverWii_GetDevicePlayerIndex(SDL_HIDAPI_Device *device, SDL_JoystickID instance_id)
{
    if (device->context) {
        return ((SDL_DriverWii_Context *)device->context)->m_playerIndex;
    }
    return -1;
}

static void
HIDAPI_DriverWii_SetDevicePlayerIndex(SDL_HIDAPI_Device *device, SDL_JoystickID instance_id, int player_index)
{
    SDL_DriverWii_Context *ctx = device->context;
    Uint8 leds;
    Uint8 data[2];

    if (!ctx) {
        return;
    }

    ctx->m_playerIndex = player_index;

    leds = ctx->m_bRumbleActive;
    /* Use the same LED codes as Smash 8-player for 5-7 */
    if (player_index == 1 || player_index > 4) {
        leds |= k_eWiiPlayerLEDs_P1;
    }
    if (player_index == 2 || player_index == 5) {
        leds |= k_eWiiPlayerLEDs_P2;
    }
    if (player_index == 3 || player_index == 6) {
        leds |= k_eWiiPlayerLEDs_P3;
    }
    if (player_index == 4 || player_index == 7) {
        leds |= k_eWiiPlayerLEDs_P4;
    }
    /* Turn on all lights for other player indexes */
    if (player_index < 1 || player_index > 7) {
        leds |= k_eWiiPlayerLEDs_P1 | k_eWiiPlayerLEDs_P2 | k_eWiiPlayerLEDs_P3 | k_eWiiPlayerLEDs_P4;
    }

    data[0] = k_eWiiOutputReportIDs_LEDs;
    data[1] = leds;
    WriteOutput(ctx, data, sizeof(data), SDL_FALSE);
}

static SDL_bool
HIDAPI_DriverWii_OpenJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
    SDL_DriverWii_Context *ctx;

    ctx = (SDL_DriverWii_Context *)SDL_calloc(1, sizeof(*ctx));
    if (!ctx) {
        SDL_OutOfMemory();
        goto error;
    }
    ctx->device = device;
    device->context = ctx;

    device->dev = SDL_hid_open_path(device->path, 0);
    if (!device->dev) {
        SDL_SetError("Couldn't open %s", device->path);
        goto error;
    }

    SDL_AddHintCallback(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,
                        SDL_GameControllerButtonReportingHintChanged, ctx);

    if (!IdentifyController(ctx, joystick)) {
        char msg[512];
        SDL_GetErrorMsg(msg, sizeof(msg) - 1);
        SDL_SetError("Couldn't read device info: %s", msg);
        goto error;
    }

    InitStickCalibrationData(ctx);
    SDL_free(device->name);
    device->name = SDL_strdup(GetNameFromExtensionInfo(ctx));

    /* Initialize the joystick capabilities */
    if (ctx->m_eExtensionControllerType == k_eWiiExtensionControllerType_WiiUPro) {
        joystick->nbuttons = 15;
    } else {
        /* Maximum is Classic Controller + Wiimote */
        joystick->nbuttons = 25;
    }
    joystick->naxes = SDL_CONTROLLER_AXIS_MAX;

    HIDAPI_DriverWii_SetDevicePlayerIndex(device, 0, SDL_JoystickGetPlayerIndex(joystick));
    RequestButtonPacketType(ctx, GetButtonPacketType(ctx));

    return SDL_TRUE;

error:
    SDL_LockMutex(device->dev_lock);
    {
        if (device->dev) {
            SDL_hid_close(device->dev);
            device->dev = NULL;
        }
        if (device->context) {
            SDL_free(device->context);
            device->context = NULL;
        }
    }
    SDL_UnlockMutex(device->dev_lock);
    return SDL_FALSE;
}

static int
HIDAPI_DriverWii_RumbleJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    SDL_DriverWii_Context *ctx = (SDL_DriverWii_Context *)device->context;
    SDL_bool active = (low_frequency_rumble || high_frequency_rumble) ? SDL_TRUE : SDL_FALSE;

    if (active != ctx->m_bRumbleActive) {
        Uint8 data[2];

        data[0] = k_eWiiOutputReportIDs_Rumble;
        data[1] = active;
        WriteOutput(ctx, data, sizeof(data), SDL_FALSE);

        ctx->m_bRumbleActive = active;
    }
    return 0;
}

static int
HIDAPI_DriverWii_RumbleJoystickTriggers(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32
HIDAPI_DriverWii_GetJoystickCapabilities(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
    return SDL_JOYCAP_RUMBLE;
}

static int
HIDAPI_DriverWii_SetJoystickLED(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int
HIDAPI_DriverWii_SendJoystickEffect(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static int
HIDAPI_DriverWii_SetJoystickSensorsEnabled(SDL_HIDAPI_Device *device, SDL_Joystick *joystick, SDL_bool enabled)
{
    /* TODO: Implement Sensors */
    return SDL_Unsupported();
}

static void PostStickCalibrated(SDL_Joystick *joystick, struct StickCalibrationData *calibration, Uint8 axis, Uint16 data)
{
    Sint16 value = 0;
    if (!calibration->center) {
        /* Center on first read */
        calibration->center = data;
        return;
    }
    if (data < calibration->min) {
        calibration->min = data;
    }
    if (data > calibration->max) {
        calibration->max = data;
    }
    if (data < calibration->center - calibration->deadzone) {
        Uint16 zero = calibration->center - calibration->deadzone;
        Uint16 range = zero - calibration->min;
        Uint16 distance = zero - data;
        float fvalue = (float)distance / (float)range;
        value = (Sint16)(fvalue * SDL_MIN_SINT16);
    } else if (data > calibration->center + calibration->deadzone) {
        Uint16 zero = calibration->center + calibration->deadzone;
        Uint16 range = calibration->max - zero;
        Uint16 distance = data - zero;
        float fvalue = (float)distance / (float)range;
        value = (Sint16)(fvalue * SDL_MAX_SINT16);
    }
    SDL_PrivateJoystickAxis(joystick, axis, value);
}

static void HandleWiiUProButtonData(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick, WiiButtonData data)
{
    static const Uint8 axes[] = { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTY };
    static const Uint8 buttons[3][8] = {
        {
            0xFF /* Unused */,                SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_START,     SDL_CONTROLLER_BUTTON_GUIDE,
            SDL_CONTROLLER_BUTTON_BACK,       SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        }, {
            SDL_CONTROLLER_BUTTON_DPAD_UP,    SDL_CONTROLLER_BUTTON_DPAD_LEFT,     0xFF /* ZR */,                   SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_A,          SDL_CONTROLLER_BUTTON_Y,             SDL_CONTROLLER_BUTTON_B,         0xFF /*ZL*/,
        }, {
            SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSTICK,     0xFF /* Charging */,             0xFF /* Plugged In */,
            0xFF /* Unused */,                0xFF /* Unused */,                   0xFF /* Unused */,               0xFF /* Unused */,
        }
    };
    Uint8 zl, zr;
    if (data.ucNExtensionBytes < 11) {
        return;
    }

    /* Sticks */
    for (int i = 0; i < 4; i++) {
        Uint16 value = data.rgucExtension[i * 2] | (data.rgucExtension[i * 2 + 1] << 8);
        PostStickCalibrated(joystick, &ctx->m_StickCalibrationData[i], axes[i], value);
    }

    /* Buttons */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 8; j++) {
            Uint8 button = buttons[i][j];
            if (button != 0xFF) {
                SDL_bool state = (data.rgucExtension[8 + i] >> j) & 1 ? SDL_RELEASED : SDL_PRESSED;
                SDL_PrivateJoystickButton(joystick, RemapButton(ctx, button), state);
            }
        }
    }

    /* Triggers */
    zl = data.rgucExtension[9] & 0x80;
    zr = data.rgucExtension[9] & 0x04;
    SDL_PrivateJoystickAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT,  zl ? SDL_MIN_SINT16 : SDL_MAX_SINT16);
    SDL_PrivateJoystickAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, zr ? SDL_MIN_SINT16 : SDL_MAX_SINT16);

    /* Power */
    UpdatePowerLevelWiiU(joystick, data.rgucExtension[10]);
}

static void HandleButtonData(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick, WiiButtonData data)
{
    if (ctx->m_eExtensionControllerType == k_eWiiExtensionControllerType_WiiUPro) {
        HandleWiiUProButtonData(ctx, joystick, data);
    }
}

static void GetBaseButtons(WiiButtonData *dst, const Uint8 *src)
{
    SDL_memcpy(dst->rgucBaseButtons, src, 2);
    dst->hasBaseButtons = SDL_TRUE;
}

static void GetAccelerometer(WiiButtonData *dst, const Uint8 *src)
{
    SDL_memcpy(dst->rgucAccelerometer, src, 3);
    dst->hasAccelerometer = SDL_TRUE;
}

static void GetExtensionData(WiiButtonData *dst, const Uint8 *src, int size)
{
    SDL_assert(size > 0 && size <= sizeof(dst->rgucExtension));
    SDL_memcpy(dst->rgucExtension, src, size);
    dst->ucNExtensionBytes = size;
}

static void HandleStatus(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick)
{
    WiiButtonData data;
    SDL_zero(data);
    GetBaseButtons(&data, ctx->m_rgucReadBuffer + 1);
    HandleButtonData(ctx, joystick, data);
    if (ctx->m_eExtensionControllerType != k_eWiiExtensionControllerType_WiiUPro) {
        /* Wii U has separate battery level tracking */
        UpdatePowerLevelWii(joystick, ctx->m_rgucReadBuffer[6]);
    }
    /* TODO: Check Extensions */
}

static void HandleResponse(SDL_DriverWii_Context *ctx)
{
}

static void HandleButtonPacket(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick)
{
    WiiButtonData data;
    SDL_zero(data);
    /* IR camera data is not supported */
    switch (ctx->m_rgucReadBuffer[0]) {
        case k_eWiiInputReportIDs_ButtonData0: /* 30 BB BB */
            GetBaseButtons(&data, ctx->m_rgucReadBuffer + 1);
            break;
        case k_eWiiInputReportIDs_ButtonData1: /* 31 BB BB AA AA AA */
        case k_eWiiInputReportIDs_ButtonData3: /* 33 BB BB AA AA AA II II II II II II II II II II II II */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetAccelerometer(&data, ctx->m_rgucReadBuffer + 3);
            break;
        case k_eWiiInputReportIDs_ButtonData2: /* 32 BB BB EE EE EE EE EE EE EE EE */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 3, 8);
            break;
        case k_eWiiInputReportIDs_ButtonData4: /* 34 BB BB EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 3, 19);
            break;
        case k_eWiiInputReportIDs_ButtonData5: /* 35 BB BB AA AA AA EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetAccelerometer(&data, ctx->m_rgucReadBuffer + 3);
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 6, 16);
            break;
        case k_eWiiInputReportIDs_ButtonData6: /* 36 BB BB II II II II II II II II II II EE EE EE EE EE EE EE EE EE */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 13, 9);
            break;
        case k_eWiiInputReportIDs_ButtonData7: /* 37 BB BB AA AA AA II II II II II II II II II II EE EE EE EE EE EE */
            GetBaseButtons  (&data, ctx->m_rgucReadBuffer + 1);
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 16, 6);
            break;
        case k_eWiiInputReportIDs_ButtonDataD: /* 3d EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE */
            GetExtensionData(&data, ctx->m_rgucReadBuffer + 1, 21);
            break;
        case k_eWiiInputReportIDs_ButtonDataE:
        case k_eWiiInputReportIDs_ButtonDataF:
        default:
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "HIDAPI WII: Unsupported button data type %02x", ctx->m_rgucReadBuffer[0]);
            return;
    }
    HandleButtonData(ctx, joystick, data);
}

static void HandleInput(SDL_DriverWii_Context *ctx, SDL_Joystick *joystick)
{
    EWiiInputReportIDs type = ctx->m_rgucReadBuffer[0];
    if (type == k_eWiiInputReportIDs_Status) {
        HandleStatus(ctx, joystick);
    } else if (type == k_eWiiInputReportIDs_Acknowledge || type == k_eWiiInputReportIDs_ReadMemory) {
        HandleResponse(ctx);
    } else if (type >= k_eWiiInputReportIDs_ButtonData0 && type <= k_eWiiInputReportIDs_ButtonDataF) {
        HandleButtonPacket(ctx, joystick);
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "HIDAPI WII: Unexpected input packet of type %x", type);
    }
}

static const Uint32 FIFTEEN_MINUTES_IN_MS = 15 * 60 * 1000;

static SDL_bool
HIDAPI_DriverWii_UpdateDevice(SDL_HIDAPI_Device *device)
{
    SDL_DriverWii_Context *ctx = (SDL_DriverWii_Context *)device->context;
    SDL_Joystick *joystick = NULL;
    int size;

    if (device->num_joysticks > 0) {
        joystick = SDL_JoystickFromInstanceID(device->joysticks[0]);
    }
    if (!joystick) {
        return SDL_FALSE;
    }

    while ((size = ReadInput(ctx)) > 0) {
#ifdef DEBUG_WII_PROTOCOL
        HIDAPI_DumpPacket("Wii packet: size = %d", ctx->m_rgucReadBuffer, size);
#endif
        HandleInput(ctx, joystick);
    }

    /* Request a status update periodically to make sure our battery value is up to date */
    if (SDL_TICKS_PASSED(SDL_GetTicks(), ctx->m_iLastStatus + FIFTEEN_MINUTES_IN_MS)) {
        ctx->m_iLastStatus = SDL_GetTicks();
        WriteOutput(ctx, (Uint8[2]){ k_eWiiOutputReportIDs_StatusRequest, ctx->m_bRumbleActive }, 2, SDL_FALSE);
    }

    if (size < 0) {
        /* Read error, device is disconnected */
        HIDAPI_JoystickDisconnected(device, joystick->instance_id);
    }
    return (size >= 0);
}

static void
HIDAPI_DriverWii_CloseJoystick(SDL_HIDAPI_Device *device, SDL_Joystick *joystick)
{
    SDL_DriverWii_Context *ctx = (SDL_DriverWii_Context *)device->context;

    SDL_DelHintCallback(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,
                        SDL_GameControllerButtonReportingHintChanged, ctx);

    SDL_LockMutex(device->dev_lock);
    {
        SDL_hid_close(device->dev);
        device->dev = NULL;

        SDL_free(device->context);
        device->context = NULL;
    }
    SDL_UnlockMutex(device->dev_lock);
}

static void
HIDAPI_DriverWii_FreeDevice(SDL_HIDAPI_Device *device)
{
}

SDL_HIDAPI_DeviceDriver SDL_HIDAPI_DriverWii =
{
    SDL_HINT_JOYSTICK_HIDAPI_WII,
    SDL_TRUE,
    HIDAPI_DriverWii_RegisterHints,
    HIDAPI_DriverWii_UnregisterHints,
    HIDAPI_DriverWii_IsEnabled,
    HIDAPI_DriverWii_IsSupportedDevice,
    HIDAPI_DriverWii_GetDeviceName,
    HIDAPI_DriverWii_InitDevice,
    HIDAPI_DriverWii_GetDevicePlayerIndex,
    HIDAPI_DriverWii_SetDevicePlayerIndex,
    HIDAPI_DriverWii_UpdateDevice,
    HIDAPI_DriverWii_OpenJoystick,
    HIDAPI_DriverWii_RumbleJoystick,
    HIDAPI_DriverWii_RumbleJoystickTriggers,
    HIDAPI_DriverWii_GetJoystickCapabilities,
    HIDAPI_DriverWii_SetJoystickLED,
    HIDAPI_DriverWii_SendJoystickEffect,
    HIDAPI_DriverWii_SetJoystickSensorsEnabled,
    HIDAPI_DriverWii_CloseJoystick,
    HIDAPI_DriverWii_FreeDevice,
};

#endif /* SDL_JOYSTICK_HIDAPI_WII */

#endif /* SDL_JOYSTICK_HIDAPI */

/* vi: set ts=4 sw=4 expandtab: */