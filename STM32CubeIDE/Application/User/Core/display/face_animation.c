#include "face_animation.h"
#include "face_assets.h"

/* Original supersampled art lives in flash. Two index rows and four RGB565
 * scanlines need 2400 bytes of RAM, rather than a 115200-byte framebuffer.
 * Changes are bounded against the previous sprite before sending any pixels. */
#define TILE_ROWS 4U
#define CYCLE_MS 12000U
#define NO_ASSET UINT16_MAX
typedef struct { uint8_t left, right, mouth; } FacePose;
static uint8_t row_new[GC9A01_WIDTH], row_old[GC9A01_WIDTH];
static uint16_t tile[GC9A01_WIDTH * TILE_ROWS];
static FacePose last_pose;
static bool pose_valid;
static uint32_t previous_ms, phase_ms;

/* Read one independently addressable RLE row. Assets contain opaque patches
 * flattened over the exact backdrop, including antialiased edge pixels. */
static HAL_StatusTypeDef decode_row(uint16_t id, uint16_t row, uint8_t *out)
{
    if (id >= FACE_ASSET_COUNT) return HAL_ERROR;
    const FaceAsset *asset = &face_assets[id];
    if (row >= asset->height || asset->width > GC9A01_WIDTH) return HAL_ERROR;
    uint32_t pos = face_rows[asset->row + row];
    const uint32_t end = face_rows[asset->row + row + 1U];
    unsigned x = 0;
    while (pos + 1U < end) {
        unsigned count = face_pixels[pos++];
        uint8_t color = face_pixels[pos++];
        if (!count || count > asset->width - x) return HAL_ERROR;
        while (count--) out[x++] = color;
    }
    return pos == end && x == asset->width ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef draw_asset(uint16_t id, uint16_t previous, uint16_t x, uint16_t y)
{
    if (id >= FACE_ASSET_COUNT) return HAL_ERROR;
    const FaceAsset *asset = &face_assets[id];
    if (x + asset->width > GC9A01_WIDTH || y + asset->height > GC9A01_HEIGHT) return HAL_ERROR;
    unsigned x0 = 0, y0 = 0, x1 = asset->width, y1 = asset->height;
    if (previous != NO_ASSET) {
        if (previous >= FACE_ASSET_COUNT || face_assets[previous].width != asset->width
            || face_assets[previous].height != asset->height) return HAL_ERROR;
        x0 = asset->width; y0 = asset->height; x1 = 0; y1 = 0;
        for (unsigned row = 0; row < asset->height; ++row) {
            if (decode_row(id, row, row_new) != HAL_OK || decode_row(previous, row, row_old) != HAL_OK)
                return HAL_ERROR;
            for (unsigned col = 0; col < asset->width; ++col) {
                if (face_palette[row_new[col]] == face_palette[row_old[col]]) continue;
                if (col < x0) x0 = col;
                if (row < y0) y0 = row;
                if (col + 1U > x1) x1 = col + 1U;
                if (row + 1U > y1) y1 = row + 1U;
            }
        }
        if (x1 == 0) return HAL_OK;
    }
    const unsigned width = x1 - x0;
    for (unsigned row = y0; row < y1; row += TILE_ROWS) {
        unsigned rows = y1 - row < TILE_ROWS ? y1 - row : TILE_ROWS;
        for (unsigned j = 0; j < rows; ++j) {
            if (decode_row(id, row + j, row_new) != HAL_OK) return HAL_ERROR;
            for (unsigned col = x0; col < x1; ++col)
                tile[j * width + col - x0] = face_palette[row_new[col]];
        }
        HAL_StatusTypeDef status = GC9A01_BlitRGB565(x + x0, y + row, width, rows, tile);
        if (status != HAL_OK) return status;
    }
    return HAL_OK;
}

/* A 12-second character performance: look around, blink, look up in wonder,
 * smile broadly, close both eyes in delight, then give a playful right wink.
 * Pose changes use elapsed time; no accumulated frame-count timing drift. */
static FacePose pose_at(uint32_t t)
{
    uint8_t eye = FACE_L_CENTER, mouth = FACE_MOUTH_SMILE;
    if (t >= 1200 && t < 1400) eye = FACE_L_LEFT_HALF;
    else if (t >= 1400 && t < 2200) eye = FACE_L_LEFT;
    else if (t >= 2200 && t < 2400) eye = FACE_L_LEFT_HALF;
    else if (t >= 2600 && t < 2800) eye = FACE_L_RIGHT_HALF;
    else if (t >= 2800 && t < 3500) eye = FACE_L_RIGHT;
    else if (t >= 3500 && t < 3700) eye = FACE_L_RIGHT_HALF;
    else if (t >= 3900 && t < 4450) {
        static const uint8_t blink[] = {FACE_L_BLINK_75, FACE_L_BLINK_50, FACE_L_BLINK_25,
            FACE_L_CLOSED, FACE_L_CLOSED, FACE_L_BLINK_25, FACE_L_BLINK_50, FACE_L_BLINK_75};
        eye = blink[(t - 3900) * 8U / 550U];
    } else if (t >= 4700 && t < 4900) eye = FACE_L_UP_HALF;
    else if (t >= 4900 && t < 5600) eye = FACE_L_UP;
    else if (t >= 5600 && t < 6000) eye = FACE_L_WIDE;
    else if (t >= 6500 && t < 7300) eye = FACE_L_HAPPY;

    if (t >= 4900 && t < 6000) mouth = FACE_MOUTH_OH;
    else if (t >= 6100 && t < 8500) mouth = FACE_MOUTH_HAPPY;
    FacePose p = {eye, eye + FACE_R_CENTER - FACE_L_CENTER, mouth};
    if (t >= 7700 && t < 8200) {
        if (t < 7800 || t >= 8100) p.right = FACE_R_BLINK_50;
        else p.right = FACE_R_CLOSED;
    }
    if (t >= 10100 && t < 10400) {
        uint8_t blink = t < 10200 || t >= 10300 ? FACE_L_BLINK_50 : FACE_L_CLOSED;
        p.left = blink;
        p.right = blink + FACE_R_CENTER - FACE_L_CENTER;
    }
    return p;
}

HAL_StatusTypeDef FaceAnimation_Init(uint32_t now_ms)
{
    previous_ms = now_ms;
    phase_ms = 0;
    pose_valid = false;
    HAL_StatusTypeDef status = draw_asset(FACE_BACKGROUND, NO_ASSET, 0, 0);
    if (status == HAL_OK) { last_pose = pose_at(0); pose_valid = true; }
    return status;
}

HAL_StatusTypeDef FaceAnimation_Update(uint32_t now_ms)
{
    phase_ms = (phase_ms + (now_ms - previous_ms) % CYCLE_MS) % CYCLE_MS;
    previous_ms = now_ms;
    FacePose pose = pose_at(phase_ms);
    HAL_StatusTypeDef status = HAL_OK;
    if (!pose_valid || pose.left != last_pose.left)
        status = draw_asset(pose.left, pose_valid ? last_pose.left : NO_ASSET, 44, 56);
    if (status == HAL_OK && (!pose_valid || pose.right != last_pose.right))
        status = draw_asset(pose.right, pose_valid ? last_pose.right : NO_ASSET, 128, 56);
    if (status == HAL_OK && (!pose_valid || pose.mouth != last_pose.mouth))
        status = draw_asset(pose.mouth, pose_valid ? last_pose.mouth : NO_ASSET, 88, 153);
    if (status == HAL_OK) { last_pose = pose; pose_valid = true; }
    else pose_valid = false;
    return status;
}
