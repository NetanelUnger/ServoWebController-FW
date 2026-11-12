#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H
/* ASCII, at most 23 bytes; also embedded in the OTA package by build_ota.bat. */
#define FIRMWARE_VERSION "1.1.0-ota.2"
_Static_assert(sizeof(FIRMWARE_VERSION) > 1 && sizeof(FIRMWARE_VERSION) <= 24,
               "Firmware version must contain 1-23 ASCII characters");
#endif
