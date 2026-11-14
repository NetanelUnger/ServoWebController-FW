#ifndef IMAGE_STORE_H
#define IMAGE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#define IMAGE_STORE_BYTES 91384U
#define IMAGE_STORE_BASE 0x08080000UL
#define IMAGE_STORE_END  0x080FA000UL
void ImageStore_Init(void);
uint8_t ImageStore_Submit(uint16_t connection, const uint8_t *p, uint16_t n, bool data);
uint16_t ImageStore_Status(uint8_t *out);
void ImageStore_Process(void);
void ImageStore_Disconnect(uint16_t connection);
bool ImageStore_ClaimOTA(void);
/* Exclusive immutable flash lease. Acquire only from task context; release also
 * safe during STOP/disconnect. All store requests and OTA are busy while held. */
bool ImageStore_AcquireTimeline(uint32_t id,const uint8_t **payload,uint32_t *length);
void ImageStore_ReleaseTimeline(void);
bool ImageStore_AcquireProgram(uint32_t id,const uint8_t **payload,uint32_t *length);
void ImageStore_ReleaseProgram(void);
/* Validated RAM snapshot; callback safe, no Flash access or storage lease. */
bool ImageStore_GetSafePose(uint16_t values[16]);
uint16_t ImageStore_RowLeft(uint16_t y);
#endif
