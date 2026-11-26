/*
 * UART protocol between core and dash (ADR 0004), shared by both firmwares.
 *
 * Frame:  0xAA 0x55 | type (1) | length (1) | payload (n) | CRC16 (2)
 *
 *   up_frame_build()      payload → frame bytes
 *   up_parser_feed()      byte stream → frames (resync, CRC, 20 ms gap)
 *   up_msg_encode()       message struct → payload
 *   up_msg_decode()       payload → message struct (length checked per type)
 *
 * Little-endian, no dynamic memory, no platform calls (the caller passes time).
 */
#ifndef UP_PROTO_H
#define UP_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "up_overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UP_PROTO_VERSION     1
#define UP_FRAME_SYNC0       0xAA
#define UP_FRAME_SYNC1       0x55
#define UP_FRAME_OVERHEAD    6        /* sync (2) + type + length + CRC16 (2) */
#define UP_PAYLOAD_MAX       255
#define UP_FRAME_MAX         (UP_PAYLOAD_MAX + UP_FRAME_OVERHEAD)   /* 261 */
#define UP_FRAME_GAP_MS      20       /* drop a partial frame after this pause */

/* ---- message types ------------------------------------------------------------ */

/* core → dash */
#define UP_MSG_STATUS        0x01
#define UP_MSG_SETTINGS      0x02
#define UP_MSG_PRESET_LIST   0x03
#define UP_MSG_PRESET_HEADER 0x04
#define UP_MSG_PRESET_CHUNK  0x05
#define UP_MSG_CALIB_STATUS  0x06
#define UP_MSG_CALIB_HISTORY 0x07
#define UP_MSG_ACK           0x7F

/* dash → core (every payload starts with `seq`) */
#define UP_CMD_SET_BRIGHTNESS     0x81
#define UP_CMD_SET_OVERLAY        0x82
#define UP_CMD_SELECT_PRESET      0x83
#define UP_CMD_RESET_TRIP         0x84
#define UP_CMD_SAVE_ODOMETER      0x85
#define UP_CMD_GET_SETTINGS       0x86
#define UP_CMD_GET_PRESET_LIST    0x87
#define UP_CMD_GET_PRESET         0x88
#define UP_CMD_PRESET_BEGIN       0x89
#define UP_CMD_PRESET_CHUNK       0x8A
#define UP_CMD_PRESET_COMMIT      0x8B
#define UP_CMD_PRESET_DELETE      0x8C
#define UP_CMD_CALIB_START        0x8D
#define UP_CMD_CALIB_STOP         0x8E
#define UP_CMD_CALIB_APPLY        0x8F
#define UP_CMD_CALIB_DISCARD      0x90
#define UP_CMD_GET_CALIB_HISTORY  0x91
#define UP_CMD_SET_SIGNAL         0x92
#define UP_CMD_SET_LIGHT          0x93
#define UP_CMD_SET_STRIP_LAYOUT   0x94
#define UP_CMD_SET_LIGHT_SETTINGS 0x95

/* ACK status (ADR 0004) */
#define UP_ACK_OK             0
#define UP_ACK_UNKNOWN        1
#define UP_ACK_INVALID        2
#define UP_ACK_CONFLICT       3
#define UP_ACK_READ_ONLY      4
#define UP_ACK_NO_FREE_SLOT   5
#define UP_ACK_NOT_FOUND      6
#define UP_ACK_BUSY           7
#define UP_ACK_CRC_MISMATCH   8
#define UP_ACK_OUT_OF_SEQ     9

/* Presets */
#define UP_CAT_PATTERN        0
#define UP_CAT_OVERLAY        1
#define UP_PRESET_NAME_LEN    16       /* bytes, UTF-8, zero-padded, no terminator on the wire */
#define UP_PRESET_FORMAT      1
#define UP_SLOT_PREVIEW       0xFF     /* PRESET_BEGIN: try on the strip, not stored */
#define UP_REVISION_NEW       0xFFFF   /* PRESET_BEGIN: target slot expected to be empty */
#define UP_PRESET_LIST_MAX    10       /* entries per PRESET_LIST frame */
#define UP_PRESET_FLAG_BUILTIN 0x01
#define UP_PRESET_FLAG_USER    0x02
#define UP_PRESET_FLAG_EMPTY   0x04
#define UP_SEGMENT_SIZE       6
#define UP_SEGMENTS_PER_CHUNK 40       /* 240 bytes */
#define UP_CHUNK_MAX          240
#define UP_PATTERN_HEADER_SIZE 23
#define UP_OVERLAY_HEADER_SIZE 25
#define UP_CALIB_HISTORY_MAX  10

/* ---- payload structs ----------------------------------------------------------- */

typedef struct {
    int16_t  speed_mm_s;      /* forward +, backward − (ADR 0015) */
    uint32_t trip_m;
    uint32_t total_m;
    uint32_t total_mm;        /* travelled distance, wraps */
    uint32_t position_mm;     /* net position, wraps; differences as int32 */
    uint8_t  active_pattern;
    uint16_t error_bits;
    uint16_t battery_mv;
    uint32_t nvs_writes;
    uint8_t  lights;          /* UP_LIGHTS_* (up_lights.h) */
} up_status_t;                /* 28 bytes */

typedef struct {
    uint8_t      protocol_version;
    uint8_t      global_brightness;
    up_overlay_t overlay;
    uint8_t      active_pattern;
    uint8_t      active_overlay;
    uint16_t     preset_list_rev;
    uint8_t      main_leds;
    uint8_t      front_leds;
    uint8_t      rear_leds;
    uint8_t      layout_flags;
    uint8_t      light_on;
    uint8_t      light_brightness;
    uint8_t      brake_flags;
    uint8_t      brake_threshold;
} up_settings_t;              /* 22 bytes */

typedef struct {
    uint8_t  slot;
    uint8_t  flags;           /* UP_PRESET_FLAG_* */
    uint16_t revision;
    uint32_t length_cm;       /* patterns only */
    char     name[UP_PRESET_NAME_LEN + 1];   /* terminated in memory */
} up_preset_entry_t;          /* 24 bytes on the wire */

typedef struct {
    uint8_t category;
    uint8_t total;
    uint8_t index;
    uint8_t count;
    up_preset_entry_t entries[UP_PRESET_LIST_MAX];
} up_preset_list_t;

typedef struct {
    uint16_t length_cm;
    uint8_t  brightness;
    uint8_t  r, g, b;
} up_segment_t;               /* 6 bytes on the wire (ADR 0011) */

/* Preset header: pattern (23 bytes) or overlay (25 bytes), depending on category */
typedef struct {
    uint8_t      format_version;
    char         name[UP_PRESET_NAME_LEN + 1];
    uint16_t     segment_count;   /* pattern */
    uint32_t     crc32;           /* pattern: over header without crc32 + segments */
    up_overlay_t overlay;         /* overlay */
} up_preset_header_t;

typedef struct {
    uint8_t            category;
    uint8_t            slot;
    uint16_t           revision;
    up_preset_header_t header;
} up_preset_header_msg_t;

typedef struct {
    uint16_t chunk_index;
    uint8_t  len;
    uint8_t  data[UP_CHUNK_MAX];
} up_chunk_t;

typedef struct {
    uint8_t state;            /* 0 idle, 1 counting, 2 stopped */
    int32_t counts;
    int32_t distance_mm;
} up_calib_status_t;

typedef struct {
    uint32_t timestamp;
    uint32_t total_m;
    uint32_t ref_cm;
    uint32_t counts;
    uint32_t nm_per_count;
} up_calib_entry_t;           /* 20 bytes */

typedef struct {
    uint32_t nm_per_count;
    uint8_t  count;
    up_calib_entry_t entries[UP_CALIB_HISTORY_MAX];
} up_calib_history_t;

typedef struct {
    uint8_t seq;
    uint8_t status;
} up_ack_t;

typedef struct {
    uint8_t            category;
    uint8_t            slot;
    uint16_t           expected_revision;
    up_preset_header_t header;
} up_preset_begin_t;

/* One message of either direction; `type` selects the member */
typedef struct {
    uint8_t type;
    uint8_t seq;              /* commands (dash → core) only */
    union {
        up_status_t            status;
        up_settings_t          settings;
        up_preset_list_t       preset_list;
        up_preset_header_msg_t preset_header;
        up_chunk_t             chunk;           /* PRESET_CHUNK, both directions */
        up_calib_status_t      calib_status;
        up_calib_history_t     calib_history;
        up_ack_t               ack;
        uint8_t                u8;              /* SET_BRIGHTNESS, GET_PRESET_LIST (category),
                                                   SET_SIGNAL, SET_LIGHT */
        up_overlay_t           overlay;         /* SET_OVERLAY */
        struct { uint8_t category, slot; } preset_ref;   /* SELECT_PRESET, GET_PRESET, PRESET_DELETE */
        up_preset_begin_t      preset_begin;
        struct { uint32_t ref_cm, timestamp; } calib_apply;
        struct { uint8_t main_leds, front_leds, rear_leds, layout_flags; } strip_layout;
        struct { uint8_t brightness, brake_flags, brake_threshold; } light_settings;
    } u;
} up_msg_t;

/* ---- functions ------------------------------------------------------------------- */

uint16_t up_crc16(const uint8_t *data, size_t len);                    /* CCITT-FALSE */
uint32_t up_crc32_update(uint32_t crc, const uint8_t *data, size_t len); /* start with 0 */

/* Builds a frame into out (at least UP_FRAME_MAX bytes); returns its length */
size_t up_frame_build(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out);

/* Payload length of a message (0 … 255), or -1 if the message is invalid */
int up_msg_encode(const up_msg_t *m, uint8_t *payload);
/* true if the payload is valid for `type`; fills m */
bool up_msg_decode(uint8_t type, const uint8_t *payload, uint8_t len, up_msg_t *m);
/* Frame for a message; returns the frame length, 0 if the message is invalid */
size_t up_msg_frame(const up_msg_t *m, uint8_t *out);

/* Segments (ADR 0011) */
void up_segment_pack(const up_segment_t *s, uint8_t *out);
void up_segment_unpack(const uint8_t *in, up_segment_t *s);
/* CRC32 of a pattern: header without crc32 field, then all packed segments */
uint32_t up_pattern_crc32(const up_preset_header_t *h, const uint8_t *segments, size_t len);

/* Byte stream parser */
typedef struct {
    uint8_t  state;
    uint8_t  type;
    uint8_t  len;
    uint16_t pos;
    uint8_t  buf[UP_PAYLOAD_MAX + 2];
    uint32_t last_ms;
    uint32_t crc_errors;
    uint32_t frames;
} up_parser_t;

void up_parser_init(up_parser_t *p);
/* Feeds one byte; returns true when a valid frame is complete (type, len, buf) */
bool up_parser_feed(up_parser_t *p, uint8_t byte, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* UP_PROTO_H */
