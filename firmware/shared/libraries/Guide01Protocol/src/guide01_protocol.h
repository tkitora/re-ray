#ifndef GUIDE01_PROTOCOL_H
#define GUIDE01_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC visibility push(default)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define G01_DEPRECATED __attribute__((deprecated))
#  define G01_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#  define G01_DEPRECATED __declspec(deprecated)
#  define G01_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
#else
#  define G01_DEPRECATED
#  define G01_DEPRECATED_MSG(msg)
#endif

/* The return value is the written length (negative on error). Ignoring it
   means sending a packet of unknown length. */
#if defined(__GNUC__) || defined(__clang__)
#  define G01_MUST_USE __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && (_MSC_VER >= 1700)
#  define G01_MUST_USE _Check_return_
#else
#  define G01_MUST_USE
#endif

const char* g01_get_version(void);

#define G01_SUCCESS           0
#define G01_ERROR_INVALID_ARG -1
#define G01_ERROR_BUFFER_TOO_SMALL -2
#define G01_ERROR_CRC_MISMATCH -3
#define G01_ERROR_INVALID_PACKET -4
#define G01_ERROR_NOT_IMPLEMENTED -5

const char* g01_get_ble_service_uuid(void);
const char* g01_get_ble_settings_service_uuid(void);
const char* g01_get_ble_settings_time_char(void);
const char* g01_get_ble_msg_notify_char(void);
const char* g01_get_ble_file_char(void);
const char* g01_get_ble_brightness_char(void);
const char* g01_get_ble_battery_level_char(void);
const char* g01_get_ble_battery_status_char(void);
const char* g01_get_ble_box_battery_char(void);
const char* g01_get_ble_weather_char(void);
const char* g01_get_ble_lyric_char(void);
const char* g01_get_ble_sleep_time_char(void);
const char* g01_get_ble_gif_text_display_char(void);
const char* g01_get_ble_cmd_char(void);
const char* g01_get_ble_motion_char(void);
const char* g01_get_ble_serial_number_uuid(void);
const char* g01_get_ble_firmware_revision_uuid(void);
const char* g01_get_ble_hardware_revision_uuid(void);
const char* g01_get_ble_software_revision_uuid(void);

/*
 * Packet builders write into a caller-supplied buffer and return the number of
 * bytes written, or a negative error code. The library keeps no internal state,
 * so the API is thread-safe and reentrant. Sizes below.
 */
#define G01_BUF_COMMAND    64    /* single commands: brightness, time, page switch */
#define G01_BUF_MOTION    128    /* motion data: 11 TLV types x 6 bytes + command */
#define G01_BUF_WEATHER   512    /* weather */
#define G01_BUF_TEXT     1024    /* notification, song info, lyrics */
#define G01_BUF_CHUNK     521    /* image / GIF Text chunk (MTU dependent) */
#define G01_BUF_GIF_TEXT 4096    /* GIF Text display and upload */
#define G01_BUF_MAX      4096    /* fits every API */

typedef struct {
    uint8_t level;
    uint8_t is_charging;
} G01BatteryData;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} G01RgbColor;

typedef struct {
    uint8_t layer_id;
    uint8_t type;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t resource_id;
    uint8_t font_size;
    G01RgbColor color;
    const char* text;
} G01DisplayItem;

typedef struct {
    uint8_t page_id;
    uint8_t touch_event;
} G01TouchEventData;

typedef struct {
    uint16_t       id;
    const uint8_t* data;
    size_t         data_size;
} G01ScanFilter;

typedef struct {
    uint8_t  id;
    uint8_t  status;
    uint32_t file_size;
    uint8_t  md5_tail[4];
} G01GifTextStatusItem;

typedef struct {
    uint8_t count;
    G01GifTextStatusItem items[10];
} G01GifTextStatusResponse;

G01_MUST_USE
int g01_parse_battery_response(const uint8_t* packet, size_t packet_len, G01BatteryData* battery);

G01_MUST_USE
int g01_brightness(uint8_t value, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_notification(const char* name, const char* title, const char* content, const char* time, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_notification_display_time(uint8_t seconds, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_weather(const char* temperature, uint8_t* buffer, size_t buffer_size);

/* timestamp is local-time epoch seconds; no time zone is applied.
   For JST, pass utc_epoch + 9*3600. */
G01_MUST_USE
int g01_time_set(uint32_t timestamp, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_page_switch_to_time(uint8_t* buffer, size_t buffer_size);

#define G01_SCREEN_SIDE_LEFT  0
#define G01_SCREEN_SIDE_RIGHT 1

G01_MUST_USE
int g01_screen_mode(uint8_t side, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_screen_mode_left(uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_screen_mode_right(uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_song_info(const char* title, const char* artist, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_lyric_data(const char* line1, const char* line2, const char* line3, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_lyric_stop(uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_lyric_text_colors(
    uint8_t title_r, uint8_t title_g, uint8_t title_b,
    uint8_t prev_r,  uint8_t prev_g,  uint8_t prev_b,
    uint8_t cur_r,   uint8_t cur_g,   uint8_t cur_b,
    uint8_t next_r,  uint8_t next_g,  uint8_t next_b,
    uint8_t* buffer, size_t buffer_size
);

G01_MUST_USE
int g01_image_info(const uint8_t* jpeg_data, size_t jpeg_size, const uint8_t* md5_hex, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_image_chunk(const uint8_t* chunk_data, size_t chunk_size, uint16_t total_chunks, uint16_t chunk_index, uint8_t* buffer, size_t buffer_size);

#define G01_IMAGE_BUILDER_SIZE_MAX     128
#define G01_GIF_TEXT_BUILDER_SIZE_MAX  128

typedef struct G01ImageBuilder G01ImageBuilder;
typedef struct G01GifTextBuilder G01GifTextBuilder;

size_t g01_image_builder_size(void);
size_t g01_gif_text_builder_size(void);

G01_MUST_USE
int g01_image_builder_init(G01ImageBuilder* b,
                           const uint8_t* jpeg_data, size_t jpeg_size, size_t mtu);
G01_MUST_USE
int g01_gif_text_builder_init_gif(G01GifTextBuilder* b, uint8_t gif_id,
                                  const uint8_t* file_data, size_t file_size, size_t mtu);
G01_MUST_USE
int g01_gif_text_builder_init_image(G01GifTextBuilder* b, uint8_t image_id,
                                    const uint8_t* file_data, size_t file_size, size_t mtu);

size_t g01_image_builder_chunk_count(const G01ImageBuilder* b);
size_t g01_gif_text_builder_chunk_count(const G01GifTextBuilder* b);

G01_MUST_USE
int g01_image_builder_info(G01ImageBuilder* b, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_builder_start(G01GifTextBuilder* b, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_image_builder_chunk(G01ImageBuilder* b, size_t idx, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_builder_chunk(G01GifTextBuilder* b, size_t idx, uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_gif_text_upload_gif(uint8_t gif_id, const uint8_t* file_data, size_t file_size, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_upload_image(uint8_t image_id, const uint8_t* file_data, size_t file_size, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_upload_gif_data(uint16_t total_packets, uint16_t current_index, const uint8_t* data, uint16_t data_size, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_upload_image_data(uint16_t total_packets, uint16_t current_index, const uint8_t* data, uint16_t data_size, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_display_gif_center(uint8_t gif_id, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_display_gif_at(uint8_t gif_id, uint16_t x, uint16_t y, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_display_gif_with_text(uint8_t gif_id, const char* text, uint16_t text_y, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_display_image_center(uint8_t image_id, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_display_elements(int show_status_bar, const G01DisplayItem* items, size_t item_count, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_remove_element(uint8_t layer_id, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_close_page(uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_status_query_gif(uint8_t gif_id, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_status_query_all_gifs(uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_gif_text_status_query_images(uint8_t* buffer, size_t buffer_size);

G01_MUST_USE
int g01_parse_gif_text_upload_response(const uint8_t* data, size_t len);
G01_MUST_USE
int g01_parse_gif_text_status_response(const uint8_t* data, size_t len, G01GifTextStatusResponse* out);
G01_MUST_USE
int g01_parse_gif_text_single_gif_status_response(const uint8_t* data, size_t len, G01GifTextStatusItem* out);
G01_MUST_USE
int g01_gif_text_validate_gif_upload(uint8_t gif_id, uint32_t file_size);
G01_MUST_USE
int g01_gif_text_validate_image_upload(uint8_t image_id, uint32_t file_size);

G01_MUST_USE
int g01_sleep_time_set(uint16_t seconds, uint8_t* buffer, size_t buffer_size);
int32_t   g01_sleep_time_parse(const uint8_t* data, size_t len);

G01_MUST_USE
int g01_resource_version_query(uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_parse_resource_version_response(const uint8_t* data, size_t len, char* out, size_t out_size);

typedef struct {
    uint8_t  type;
    uint32_t value;
} G01MotionItem;

G01_MUST_USE
int g01_motion_set_layout(uint8_t groups, uint8_t* buffer, size_t buffer_size);
G01_MUST_USE
int g01_motion_data(const G01MotionItem* items, size_t count, uint8_t* buffer, size_t buffer_size);

G01ScanFilter g01_get_scan_filter(void);

G01_MUST_USE
int g01_parse_touch_event(const uint8_t* data, size_t data_len, G01TouchEventData* touch_event);

typedef struct {
    uint8_t notify_type;
    uint8_t single_click, double_click, swipe_forward, swipe_back, long_press;
} G01TouchConsts;
G01TouchConsts g01_get_touch_consts(void);

typedef struct {
    uint8_t  element_type_gif, element_type_image, element_type_text;
    uint8_t  result_ready, result_invalid_id, result_file_too_large;
    uint8_t  result_no_storage, result_md5_failed, result_write_failed, result_packet_lost;
    uint8_t  status_not_exists, status_uploaded;
    uint16_t screen_width, screen_height, content_height, pos_center;
    uint32_t max_gif_size;
    uint8_t  max_gif_count;
    uint8_t  max_image_count;
    uint8_t  cmd_gif_upload_start;
    uint8_t  cmd_gif_status_query;
    uint8_t  cmd_image_upload_start;
    uint8_t  cmd_image_status_query;
} G01GifTextConsts;
G01GifTextConsts g01_get_gif_text_consts(void);

typedef struct {
    uint8_t cmd_info, cmd_chunk, cmd_clear;
    uint8_t chunk_overhead;
} G01ImageConsts;
G01ImageConsts g01_get_image_consts(void);

typedef struct {
    uint8_t cmd_set_layout, cmd_data;
    uint8_t layout_one, layout_two, layout_three, layout_five;
} G01MotionCmdConsts;
G01MotionCmdConsts g01_get_motion_cmd_consts(void);

typedef struct {
    uint8_t distance, speed, avg_speed, max_speed;
    uint8_t heart_rate, max_heart_rate;
    uint8_t cadence, max_cadence;
    uint8_t power, max_power;
    uint8_t sport_time;   /* seconds */
} G01MotionTypeConsts;
G01MotionTypeConsts g01_get_motion_type_consts(void);

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif
