/*
 * Unit test for the UART protocol (ADR 0004), common/src/up_proto.c.
 *
 *   cc -Wall -Wextra -I common/include common/test/test_proto.c common/src/up_proto.c -o /tmp/test_proto && /tmp/test_proto
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "up_proto.h"

static int s_tests;

/* Encode, check the payload size, frame it, parse it byte by byte, decode, compare */
static void roundtrip(const up_msg_t *in, int expected_len)
{
    uint8_t payload[UP_PAYLOAD_MAX];
    int len = up_msg_encode(in, payload);
    if (len != expected_len) {
        printf("FAIL: type 0x%02X payload %d bytes, expected %d\n", in->type, len, expected_len);
        assert(0);
    }
    uint8_t frame[UP_FRAME_MAX];
    size_t n = up_msg_frame(in, frame);
    assert(n == (size_t)len + UP_FRAME_OVERHEAD);

    up_parser_t p;
    up_parser_init(&p);
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        bool r = up_parser_feed(&p, frame[i], 1000);
        assert(r == (i == n - 1));
        done |= r;
    }
    assert(done && p.type == in->type && p.len == len);

    up_msg_t out;
    assert(up_msg_decode(p.type, p.buf, p.len, &out));
    /* re-encode the decoded message: must give identical bytes */
    uint8_t again[UP_PAYLOAD_MAX];
    assert(up_msg_encode(&out, again) == len && memcmp(again, payload, (size_t)len) == 0);
    /* wrong length is rejected (one byte more and one less) */
    uint8_t longer[UP_PAYLOAD_MAX + 1];
    memcpy(longer, payload, (size_t)len);
    longer[len] = 0;
    if (len < UP_PAYLOAD_MAX && in->type != UP_MSG_PRESET_CHUNK && in->type != UP_CMD_PRESET_CHUNK) {
        assert(!up_msg_decode(in->type, longer, (uint8_t)(len + 1), &out));
    }
    if (len > 0 && in->type != UP_MSG_PRESET_CHUNK && in->type != UP_CMD_PRESET_CHUNK) {
        assert(!up_msg_decode(in->type, payload, (uint8_t)(len - 1), &out));
    }
    s_tests++;
}

static void test_crc(void)
{
    const uint8_t check[] = "123456789";
    assert(up_crc16(check, 9) == 0x29B1);              /* CRC-16/CCITT-FALSE */
    assert(up_crc32_update(0, check, 9) == 0xCBF43926);   /* CRC-32/ISO-HDLC */
    /* CRC32 in two parts gives the same result */
    assert(up_crc32_update(up_crc32_update(0, check, 4), check + 4, 5) == 0xCBF43926);
}

static void test_status_and_settings(void)
{
    up_msg_t m = {.type = UP_MSG_STATUS};
    m.u.status = (up_status_t){-1234, 12390, 1234567, 0xFFFFFF00u, 0x80000001u, 3, 0x8001, 7420, 4711, 0x15};
    roundtrip(&m, 28);

    /* byte layout of STATUS, little-endian */
    uint8_t p[UP_PAYLOAD_MAX];
    up_msg_encode(&m, p);
    assert(p[0] == 0x2E && p[1] == 0xFB);   /* -1234 */
    assert(p[10] == 0x00 && p[11] == 0xFF && p[12] == 0xFF && p[13] == 0xFF);   /* total_mm */
    assert(p[27] == 0x15);                  /* lights last */

    up_msg_t s = {.type = UP_MSG_SETTINGS};
    s.u.settings = (up_settings_t){UP_PROTO_VERSION, 80, {1, 10, 100, 20, 150, 300}, 2, 9, 0x1234,
                                   101, 4, 8, 0x01, 1, 60, 1, 12};
    roundtrip(&s, 22);
}

static void test_presets(void)
{
    up_msg_t m = {.type = UP_MSG_PRESET_LIST};
    up_preset_list_t *l = &m.u.preset_list;
    l->category = UP_CAT_OVERLAY;
    l->total = 13;
    l->index = 10;
    l->count = 3;
    for (int i = 0; i < 3; i++) {
        l->entries[i] = (up_preset_entry_t){(uint8_t)(8 + i), UP_PRESET_FLAG_USER, (uint16_t)(i + 1), 0, ""};
        snprintf(l->entries[i].name, sizeof(l->entries[i].name), "Overlay %d", i);
    }
    strcpy(l->entries[2].name, "Weiche Ränd (2)");   /* 16 bytes UTF-8, no room for a terminator on the wire */
    roundtrip(&m, 4 + 3 * 24);
    up_msg_t full = m;
    full.u.preset_list.count = UP_PRESET_LIST_MAX;
    roundtrip(&full, 4 + 10 * 24);   /* 244 bytes */
    full.u.preset_list.count = UP_PRESET_LIST_MAX + 1;
    uint8_t p[UP_PAYLOAD_MAX];
    assert(up_msg_encode(&full, p) == -1);

    /* name survives the 16-byte field */
    up_msg_t out;
    int len = up_msg_encode(&m, p);
    assert(up_msg_decode(UP_MSG_PRESET_LIST, p, (uint8_t)len, &out));
    assert(strcmp(out.u.preset_list.entries[2].name, "Weiche Ränd (2)") == 0);

    /* pattern header (23 bytes) and overlay header (25 bytes) */
    up_msg_t h = {.type = UP_MSG_PRESET_HEADER};
    h.u.preset_header = (up_preset_header_msg_t){UP_CAT_PATTERN, 16, 7, {UP_PRESET_FORMAT, "Streifen", 4, 0xDEADBEEF, {0}}};
    roundtrip(&h, 4 + UP_PATTERN_HEADER_SIZE);
    h.u.preset_header.category = UP_CAT_OVERLAY;
    h.u.preset_header.header.overlay = (up_overlay_t){1, 0, 100, 0, 250, 250};
    h.u.preset_header.header.segment_count = 0;
    h.u.preset_header.header.crc32 = 0;
    roundtrip(&h, 4 + UP_OVERLAY_HEADER_SIZE);
    h.u.preset_header.category = 7;
    assert(up_msg_encode(&h, p) == -1);

    up_msg_t b = {.type = UP_CMD_PRESET_BEGIN, .seq = 42};
    b.u.preset_begin = (up_preset_begin_t){UP_CAT_PATTERN, UP_SLOT_PREVIEW, UP_REVISION_NEW,
                                           {UP_PRESET_FORMAT, "Vorschau", 2048, 1, {0}}};
    roundtrip(&b, 1 + 4 + UP_PATTERN_HEADER_SIZE);
    b.u.preset_begin.category = UP_CAT_OVERLAY;
    b.u.preset_begin.header.segment_count = 0;
    b.u.preset_begin.header.crc32 = 0;
    b.u.preset_begin.header.overlay = (up_overlay_t){1, 20, 100, 20, 150, 150};
    roundtrip(&b, 1 + 4 + UP_OVERLAY_HEADER_SIZE);
}

/* A full pattern of 2048 segments in 52 chunks of 40 segments (ADR 0004, 0011) */
static void test_pattern_transfer(void)
{
    static uint8_t data[2048 * UP_SEGMENT_SIZE];
    for (int i = 0; i < 2048; i++) {
        up_segment_t s = {(uint16_t)(i + 1), (uint8_t)(i % 101), (uint8_t)i, (uint8_t)(i >> 3), 0x40};
        up_segment_pack(&s, data + i * UP_SEGMENT_SIZE);
    }
    up_segment_t back;
    up_segment_unpack(data + 6 * 300, &back);
    assert(back.length_cm == 301 && back.brightness == 300 % 101 && back.r == (uint8_t)300 && back.b == 0x40);
    assert(data[0] == 1 && data[1] == 0);   /* length_cm little-endian */

    up_preset_header_t h = {UP_PRESET_FORMAT, "10-m-Muster", 2048, 0, {0}};
    h.crc32 = up_pattern_crc32(&h, data, sizeof(data));

    /* send chunks, receive and reassemble */
    static uint8_t rx[sizeof(data)];
    size_t rx_len = 0;
    int chunks = 0;
    up_parser_t p;
    up_parser_init(&p);
    for (size_t off = 0; off < sizeof(data); off += UP_CHUNK_MAX) {
        up_msg_t c = {.type = UP_CMD_PRESET_CHUNK, .seq = (uint8_t)chunks};
        c.u.chunk.chunk_index = (uint16_t)chunks;
        c.u.chunk.len = (uint8_t)(sizeof(data) - off < UP_CHUNK_MAX ? sizeof(data) - off : UP_CHUNK_MAX);
        memcpy(c.u.chunk.data, data + off, c.u.chunk.len);
        uint8_t frame[UP_FRAME_MAX];
        size_t n = up_msg_frame(&c, frame);
        assert(n > 0);
        for (size_t i = 0; i < n; i++) {
            if (up_parser_feed(&p, frame[i], 0)) {
                up_msg_t d;
                assert(up_msg_decode(p.type, p.buf, p.len, &d));
                assert(d.u.chunk.chunk_index == chunks);
                memcpy(rx + rx_len, d.u.chunk.data, d.u.chunk.len);
                rx_len += d.u.chunk.len;
            }
        }
        chunks++;
    }
    assert(chunks == 52 && rx_len == sizeof(data));
    assert(up_pattern_crc32(&h, rx, rx_len) == h.crc32);
    rx[1000] ^= 1;
    assert(up_pattern_crc32(&h, rx, rx_len) != h.crc32);   /* commit would answer CRC mismatch */

    up_msg_t big = {.type = UP_CMD_PRESET_CHUNK};
    big.u.chunk.len = UP_CHUNK_MAX + 1;
    uint8_t pl[UP_PAYLOAD_MAX];
    assert(up_msg_encode(&big, pl) == -1);
}

static void test_other_messages(void)
{
    up_msg_t m;

    m = (up_msg_t){.type = UP_MSG_CALIB_STATUS, .u.calib_status = {1, -579489, -15356}};
    roundtrip(&m, 9);

    m = (up_msg_t){.type = UP_MSG_CALIB_HISTORY};
    m.u.calib_history.nm_per_count = 26500;
    m.u.calib_history.count = UP_CALIB_HISTORY_MAX;
    for (int i = 0; i < UP_CALIB_HISTORY_MAX; i++) {
        m.u.calib_history.entries[i] = (up_calib_entry_t){1795003600u - (uint32_t)i, 1234567, 1000, 37736, 26500};
    }
    roundtrip(&m, 5 + 10 * 20);

    m = (up_msg_t){.type = UP_MSG_ACK, .u.ack = {200, UP_ACK_CONFLICT}};
    roundtrip(&m, 2);

    m = (up_msg_t){.type = UP_MSG_PRESET_CHUNK, .u.chunk = {.chunk_index = 51, .len = 48}};
    roundtrip(&m, 2 + 48);

    /* commands */
    const uint8_t no_args[] = {UP_CMD_RESET_TRIP, UP_CMD_SAVE_ODOMETER, UP_CMD_GET_SETTINGS, UP_CMD_PRESET_COMMIT,
                               UP_CMD_CALIB_START, UP_CMD_CALIB_STOP, UP_CMD_CALIB_DISCARD, UP_CMD_GET_CALIB_HISTORY};
    for (size_t i = 0; i < sizeof(no_args); i++) {
        m = (up_msg_t){.type = no_args[i], .seq = (uint8_t)i};
        roundtrip(&m, 1);
    }
    const uint8_t one_byte[] = {UP_CMD_SET_BRIGHTNESS, UP_CMD_GET_PRESET_LIST, UP_CMD_SET_SIGNAL, UP_CMD_SET_LIGHT};
    for (size_t i = 0; i < sizeof(one_byte); i++) {
        m = (up_msg_t){.type = one_byte[i], .seq = 7, .u.u8 = 3};
        roundtrip(&m, 2);
    }
    const uint8_t refs[] = {UP_CMD_SELECT_PRESET, UP_CMD_GET_PRESET, UP_CMD_PRESET_DELETE};
    for (size_t i = 0; i < sizeof(refs); i++) {
        m = (up_msg_t){.type = refs[i], .seq = 1, .u.preset_ref = {UP_CAT_OVERLAY, 9}};
        roundtrip(&m, 3);
    }
    m = (up_msg_t){.type = UP_CMD_SET_OVERLAY, .seq = 9, .u.overlay = {1, 30, 90, 60, 100, 200}};
    roundtrip(&m, 9);
    m = (up_msg_t){.type = UP_CMD_CALIB_APPLY, .seq = 9, .u.calib_apply = {1000, 1795003600u}};
    roundtrip(&m, 9);
    m = (up_msg_t){.type = UP_CMD_SET_STRIP_LAYOUT, .seq = 9, .u.strip_layout = {101, 4, 8, 3}};
    roundtrip(&m, 5);
    m = (up_msg_t){.type = UP_CMD_SET_LIGHT_SETTINGS, .seq = 9, .u.light_settings = {60, 1, 10}};
    roundtrip(&m, 4);

    /* unknown types */
    m = (up_msg_t){.type = 0x42};
    uint8_t p[UP_PAYLOAD_MAX];
    assert(up_msg_encode(&m, p) == -1);
    assert(!up_msg_decode(0xC0, p, 1, &m));
}

static void test_parser_robustness(void)
{
    up_msg_t ack = {.type = UP_MSG_ACK, .u.ack = {5, UP_ACK_OK}};
    uint8_t frame[UP_FRAME_MAX];
    size_t n = up_msg_frame(&ack, frame);

    up_parser_t p;
    up_parser_init(&p);
    int found = 0;
    /* garbage, a lone sync byte, 0xAA 0xAA 0x55 before the frame */
    const uint8_t junk[] = {0x00, 0x55, 0xAA, 0x12, 0xAA, 0xAA};
    for (size_t i = 0; i < sizeof(junk); i++) found += up_parser_feed(&p, junk[i], 0);
    for (size_t i = 0; i < n; i++) found += up_parser_feed(&p, frame[i], 0);
    assert(found == 1);

    /* corrupted CRC: dropped, counted; the next frame is received */
    frame[n - 1] ^= 0xFF;
    for (size_t i = 0; i < n; i++) found += up_parser_feed(&p, frame[i], 0);
    assert(found == 1 && p.crc_errors == 1);
    frame[n - 1] ^= 0xFF;
    for (size_t i = 0; i < n; i++) found += up_parser_feed(&p, frame[i], 0);
    assert(found == 2);

    /* frame cut off, pause > 20 ms, then a complete frame */
    for (size_t i = 0; i < 4; i++) up_parser_feed(&p, frame[i], 100);
    for (size_t i = 0; i < n; i++) found += up_parser_feed(&p, frame[i], 130);
    assert(found == 3);

    /* small pauses inside a frame are fine */
    for (size_t i = 0; i < n; i++) found += up_parser_feed(&p, frame[i], 200 + (uint32_t)i * 15);
    assert(found == 4);

    /* empty payload */
    uint8_t f0[UP_FRAME_MAX];
    size_t n0 = up_frame_build(UP_CMD_GET_SETTINGS, NULL, 0, f0);
    assert(n0 == UP_FRAME_OVERHEAD);
    for (size_t i = 0; i < n0; i++) found += up_parser_feed(&p, f0[i], 300);
    assert(found == 5 && p.len == 0);
    up_msg_t d;
    assert(!up_msg_decode(UP_CMD_GET_SETTINGS, p.buf, p.len, &d));   /* seq missing */

    /* maximum frame */
    uint8_t big[UP_PAYLOAD_MAX];
    memset(big, 0xAA, sizeof(big));
    size_t nb = up_frame_build(0x05, big, UP_PAYLOAD_MAX, frame);
    assert(nb == UP_FRAME_MAX);
    for (size_t i = 0; i < nb; i++) found += up_parser_feed(&p, frame[i], 400);
    assert(found == 6 && p.len == UP_PAYLOAD_MAX && memcmp(p.buf, big, UP_PAYLOAD_MAX) == 0);
}

int main(void)
{
    test_crc();
    test_status_and_settings();
    test_presets();
    test_pattern_transfer();
    test_other_messages();
    test_parser_robustness();
    printf("test_proto: all passed (%d message round trips)\n", s_tests);
    return 0;
}
