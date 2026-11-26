/* UART protocol between core and dash (ADR 0004), see up_proto.h. */
#include "up_proto.h"

#include <string.h>

/* ---- CRC ----------------------------------------------------------------------- */

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t up_crc16(const uint8_t *data, size_t len)
{
    return crc16_update(0xFFFF, data, len);
}

uint32_t up_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

/* ---- little-endian writer and reader ------------------------------------------- */

typedef struct {
    uint8_t *p;
    size_t n;
} wr_t;

static void w8(wr_t *w, uint8_t v) { w->p[w->n++] = v; }
static void w16(wr_t *w, uint16_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); }
static void w32(wr_t *w, uint32_t v) { w16(w, (uint16_t)v); w16(w, (uint16_t)(v >> 16)); }

static void wname(wr_t *w, const char *name)
{
    size_t len = strnlen(name, UP_PRESET_NAME_LEN);
    memcpy(w->p + w->n, name, len);
    memset(w->p + w->n + len, 0, UP_PRESET_NAME_LEN - len);
    w->n += UP_PRESET_NAME_LEN;
}

static void woverlay(wr_t *w, const up_overlay_t *o)
{
    w8(w, o->type);
    w8(w, o->level_start);
    w8(w, o->level_mid);
    w8(w, o->level_end);
    w16(w, o->fade_start_mm);
    w16(w, o->fade_end_mm);
}

typedef struct {
    const uint8_t *p;
    size_t len;
    size_t pos;
    bool ok;
} rd_t;

static uint8_t r8(rd_t *r)
{
    if (r->pos + 1 > r->len) {
        r->ok = false;
        return 0;
    }
    return r->p[r->pos++];
}
static uint16_t r16(rd_t *r) { uint16_t lo = r8(r); return (uint16_t)(lo | (uint16_t)r8(r) << 8); }
static uint32_t r32(rd_t *r) { uint32_t lo = r16(r); return lo | (uint32_t)r16(r) << 16; }

static void rname(rd_t *r, char *name)
{
    for (int i = 0; i < UP_PRESET_NAME_LEN; i++) {
        name[i] = (char)r8(r);
    }
    name[UP_PRESET_NAME_LEN] = '\0';
}

static void roverlay(rd_t *r, up_overlay_t *o)
{
    o->type = r8(r);
    o->level_start = r8(r);
    o->level_mid = r8(r);
    o->level_end = r8(r);
    o->fade_start_mm = r16(r);
    o->fade_end_mm = r16(r);
}

/* ---- presets --------------------------------------------------------------------- */

void up_segment_pack(const up_segment_t *s, uint8_t *out)
{
    wr_t w = {out, 0};
    w16(&w, s->length_cm);
    w8(&w, s->brightness);
    w8(&w, s->r);
    w8(&w, s->g);
    w8(&w, s->b);
}

void up_segment_unpack(const uint8_t *in, up_segment_t *s)
{
    rd_t r = {in, UP_SEGMENT_SIZE, 0, true};
    s->length_cm = r16(&r);
    s->brightness = r8(&r);
    s->r = r8(&r);
    s->g = r8(&r);
    s->b = r8(&r);
}

static void wheader(wr_t *w, uint8_t category, const up_preset_header_t *h)
{
    w8(w, h->format_version);
    wname(w, h->name);
    if (category == UP_CAT_PATTERN) {
        w16(w, h->segment_count);
        w32(w, h->crc32);
    } else {
        woverlay(w, &h->overlay);
    }
}

static void rheader(rd_t *r, uint8_t category, up_preset_header_t *h)
{
    memset(h, 0, sizeof(*h));
    h->format_version = r8(r);
    rname(r, h->name);
    if (category == UP_CAT_PATTERN) {
        h->segment_count = r16(r);
        h->crc32 = r32(r);
    } else {
        roverlay(r, &h->overlay);
    }
}

uint32_t up_pattern_crc32(const up_preset_header_t *h, const uint8_t *segments, size_t len)
{
    uint8_t head[UP_PATTERN_HEADER_SIZE];
    wr_t w = {head, 0};
    w8(&w, h->format_version);
    wname(&w, h->name);
    w16(&w, h->segment_count);   /* 19 bytes: header without crc32 */
    uint32_t crc = up_crc32_update(0, head, w.n);
    return up_crc32_update(crc, segments, len);
}

/* ---- messages ---------------------------------------------------------------------- */

static bool valid_category(uint8_t c)
{
    return c == UP_CAT_PATTERN || c == UP_CAT_OVERLAY;
}

static bool is_command(uint8_t type)
{
    return type >= 0x80;
}

int up_msg_encode(const up_msg_t *m, uint8_t *payload)
{
    wr_t w = {payload, 0};
    if (is_command(m->type)) {
        w8(&w, m->seq);
    }
    switch (m->type) {
    case UP_MSG_STATUS: {
        const up_status_t *s = &m->u.status;
        w16(&w, (uint16_t)s->speed_mm_s);
        w32(&w, s->trip_m);
        w32(&w, s->total_m);
        w32(&w, s->total_mm);
        w32(&w, s->position_mm);
        w8(&w, s->active_pattern);
        w16(&w, s->error_bits);
        w16(&w, s->battery_mv);
        w32(&w, s->nvs_writes);
        w8(&w, s->lights);
        break;
    }
    case UP_MSG_SETTINGS: {
        const up_settings_t *s = &m->u.settings;
        w8(&w, s->protocol_version);
        w8(&w, s->global_brightness);
        woverlay(&w, &s->overlay);
        w8(&w, s->active_pattern);
        w8(&w, s->active_overlay);
        w16(&w, s->preset_list_rev);
        w8(&w, s->main_leds);
        w8(&w, s->front_leds);
        w8(&w, s->rear_leds);
        w8(&w, s->layout_flags);
        w8(&w, s->light_on);
        w8(&w, s->light_brightness);
        w8(&w, s->brake_flags);
        w8(&w, s->brake_threshold);
        break;
    }
    case UP_MSG_PRESET_LIST: {
        const up_preset_list_t *l = &m->u.preset_list;
        if (l->count > UP_PRESET_LIST_MAX) {
            return -1;
        }
        w8(&w, l->category);
        w8(&w, l->total);
        w8(&w, l->index);
        w8(&w, l->count);
        for (int i = 0; i < l->count; i++) {
            const up_preset_entry_t *e = &l->entries[i];
            w8(&w, e->slot);
            w8(&w, e->flags);
            w16(&w, e->revision);
            w32(&w, e->length_cm);
            wname(&w, e->name);
        }
        break;
    }
    case UP_MSG_PRESET_HEADER: {
        const up_preset_header_msg_t *h = &m->u.preset_header;
        if (!valid_category(h->category)) {
            return -1;
        }
        w8(&w, h->category);
        w8(&w, h->slot);
        w16(&w, h->revision);
        wheader(&w, h->category, &h->header);
        break;
    }
    case UP_MSG_PRESET_CHUNK:
    case UP_CMD_PRESET_CHUNK:
        if (m->u.chunk.len > UP_CHUNK_MAX) {
            return -1;
        }
        w16(&w, m->u.chunk.chunk_index);
        memcpy(w.p + w.n, m->u.chunk.data, m->u.chunk.len);
        w.n += m->u.chunk.len;
        break;
    case UP_MSG_CALIB_STATUS:
        w8(&w, m->u.calib_status.state);
        w32(&w, (uint32_t)m->u.calib_status.counts);
        w32(&w, (uint32_t)m->u.calib_status.distance_mm);
        break;
    case UP_MSG_CALIB_HISTORY: {
        const up_calib_history_t *h = &m->u.calib_history;
        if (h->count > UP_CALIB_HISTORY_MAX) {
            return -1;
        }
        w32(&w, h->nm_per_count);
        w8(&w, h->count);
        for (int i = 0; i < h->count; i++) {
            w32(&w, h->entries[i].timestamp);
            w32(&w, h->entries[i].total_m);
            w32(&w, h->entries[i].ref_cm);
            w32(&w, h->entries[i].counts);
            w32(&w, h->entries[i].nm_per_count);
        }
        break;
    }
    case UP_MSG_ACK:
        w8(&w, m->u.ack.seq);
        w8(&w, m->u.ack.status);
        break;

    case UP_CMD_SET_BRIGHTNESS:
    case UP_CMD_GET_PRESET_LIST:
    case UP_CMD_SET_SIGNAL:
    case UP_CMD_SET_LIGHT:
        w8(&w, m->u.u8);
        break;
    case UP_CMD_SET_OVERLAY:
        woverlay(&w, &m->u.overlay);
        break;
    case UP_CMD_SELECT_PRESET:
    case UP_CMD_GET_PRESET:
    case UP_CMD_PRESET_DELETE:
        w8(&w, m->u.preset_ref.category);
        w8(&w, m->u.preset_ref.slot);
        break;
    case UP_CMD_RESET_TRIP:
    case UP_CMD_SAVE_ODOMETER:
    case UP_CMD_GET_SETTINGS:
    case UP_CMD_PRESET_COMMIT:
    case UP_CMD_CALIB_START:
    case UP_CMD_CALIB_STOP:
    case UP_CMD_CALIB_DISCARD:
    case UP_CMD_GET_CALIB_HISTORY:
        break;
    case UP_CMD_PRESET_BEGIN: {
        const up_preset_begin_t *b = &m->u.preset_begin;
        if (!valid_category(b->category)) {
            return -1;
        }
        w8(&w, b->category);
        w8(&w, b->slot);
        w16(&w, b->expected_revision);
        wheader(&w, b->category, &b->header);
        break;
    }
    case UP_CMD_CALIB_APPLY:
        w32(&w, m->u.calib_apply.ref_cm);
        w32(&w, m->u.calib_apply.timestamp);
        break;
    case UP_CMD_SET_STRIP_LAYOUT:
        w8(&w, m->u.strip_layout.main_leds);
        w8(&w, m->u.strip_layout.front_leds);
        w8(&w, m->u.strip_layout.rear_leds);
        w8(&w, m->u.strip_layout.layout_flags);
        break;
    case UP_CMD_SET_LIGHT_SETTINGS:
        w8(&w, m->u.light_settings.brightness);
        w8(&w, m->u.light_settings.brake_flags);
        w8(&w, m->u.light_settings.brake_threshold);
        break;
    default:
        return -1;
    }
    return (int)w.n;
}

bool up_msg_decode(uint8_t type, const uint8_t *payload, uint8_t len, up_msg_t *m)
{
    rd_t r = {payload, len, 0, true};
    memset(m, 0, sizeof(*m));
    m->type = type;
    if (is_command(type)) {
        m->seq = r8(&r);
    }
    switch (type) {
    case UP_MSG_STATUS: {
        up_status_t *s = &m->u.status;
        s->speed_mm_s = (int16_t)r16(&r);
        s->trip_m = r32(&r);
        s->total_m = r32(&r);
        s->total_mm = r32(&r);
        s->position_mm = r32(&r);
        s->active_pattern = r8(&r);
        s->error_bits = r16(&r);
        s->battery_mv = r16(&r);
        s->nvs_writes = r32(&r);
        s->lights = r8(&r);
        break;
    }
    case UP_MSG_SETTINGS: {
        up_settings_t *s = &m->u.settings;
        s->protocol_version = r8(&r);
        s->global_brightness = r8(&r);
        roverlay(&r, &s->overlay);
        s->active_pattern = r8(&r);
        s->active_overlay = r8(&r);
        s->preset_list_rev = r16(&r);
        s->main_leds = r8(&r);
        s->front_leds = r8(&r);
        s->rear_leds = r8(&r);
        s->layout_flags = r8(&r);
        s->light_on = r8(&r);
        s->light_brightness = r8(&r);
        s->brake_flags = r8(&r);
        s->brake_threshold = r8(&r);
        break;
    }
    case UP_MSG_PRESET_LIST: {
        up_preset_list_t *l = &m->u.preset_list;
        l->category = r8(&r);
        l->total = r8(&r);
        l->index = r8(&r);
        l->count = r8(&r);
        if (l->count > UP_PRESET_LIST_MAX) {
            return false;
        }
        for (int i = 0; i < l->count; i++) {
            up_preset_entry_t *e = &l->entries[i];
            e->slot = r8(&r);
            e->flags = r8(&r);
            e->revision = r16(&r);
            e->length_cm = r32(&r);
            rname(&r, e->name);
        }
        break;
    }
    case UP_MSG_PRESET_HEADER: {
        up_preset_header_msg_t *h = &m->u.preset_header;
        h->category = r8(&r);
        if (!r.ok || !valid_category(h->category)) {
            return false;
        }
        h->slot = r8(&r);
        h->revision = r16(&r);
        rheader(&r, h->category, &h->header);
        break;
    }
    case UP_MSG_PRESET_CHUNK:
    case UP_CMD_PRESET_CHUNK:
        m->u.chunk.chunk_index = r16(&r);
        if (!r.ok || len - r.pos > UP_CHUNK_MAX) {
            return false;
        }
        m->u.chunk.len = (uint8_t)(len - r.pos);
        memcpy(m->u.chunk.data, payload + r.pos, m->u.chunk.len);
        r.pos = len;
        break;
    case UP_MSG_CALIB_STATUS:
        m->u.calib_status.state = r8(&r);
        m->u.calib_status.counts = (int32_t)r32(&r);
        m->u.calib_status.distance_mm = (int32_t)r32(&r);
        break;
    case UP_MSG_CALIB_HISTORY: {
        up_calib_history_t *h = &m->u.calib_history;
        h->nm_per_count = r32(&r);
        h->count = r8(&r);
        if (h->count > UP_CALIB_HISTORY_MAX) {
            return false;
        }
        for (int i = 0; i < h->count; i++) {
            h->entries[i].timestamp = r32(&r);
            h->entries[i].total_m = r32(&r);
            h->entries[i].ref_cm = r32(&r);
            h->entries[i].counts = r32(&r);
            h->entries[i].nm_per_count = r32(&r);
        }
        break;
    }
    case UP_MSG_ACK:
        m->u.ack.seq = r8(&r);
        m->u.ack.status = r8(&r);
        break;

    case UP_CMD_SET_BRIGHTNESS:
    case UP_CMD_GET_PRESET_LIST:
    case UP_CMD_SET_SIGNAL:
    case UP_CMD_SET_LIGHT:
        m->u.u8 = r8(&r);
        break;
    case UP_CMD_SET_OVERLAY:
        roverlay(&r, &m->u.overlay);
        break;
    case UP_CMD_SELECT_PRESET:
    case UP_CMD_GET_PRESET:
    case UP_CMD_PRESET_DELETE:
        m->u.preset_ref.category = r8(&r);
        m->u.preset_ref.slot = r8(&r);
        break;
    case UP_CMD_RESET_TRIP:
    case UP_CMD_SAVE_ODOMETER:
    case UP_CMD_GET_SETTINGS:
    case UP_CMD_PRESET_COMMIT:
    case UP_CMD_CALIB_START:
    case UP_CMD_CALIB_STOP:
    case UP_CMD_CALIB_DISCARD:
    case UP_CMD_GET_CALIB_HISTORY:
        break;
    case UP_CMD_PRESET_BEGIN: {
        up_preset_begin_t *b = &m->u.preset_begin;
        b->category = r8(&r);
        if (!r.ok || !valid_category(b->category)) {
            return false;
        }
        b->slot = r8(&r);
        b->expected_revision = r16(&r);
        rheader(&r, b->category, &b->header);
        break;
    }
    case UP_CMD_CALIB_APPLY:
        m->u.calib_apply.ref_cm = r32(&r);
        m->u.calib_apply.timestamp = r32(&r);
        break;
    case UP_CMD_SET_STRIP_LAYOUT:
        m->u.strip_layout.main_leds = r8(&r);
        m->u.strip_layout.front_leds = r8(&r);
        m->u.strip_layout.rear_leds = r8(&r);
        m->u.strip_layout.layout_flags = r8(&r);
        break;
    case UP_CMD_SET_LIGHT_SETTINGS:
        m->u.light_settings.brightness = r8(&r);
        m->u.light_settings.brake_flags = r8(&r);
        m->u.light_settings.brake_threshold = r8(&r);
        break;
    default:
        return false;
    }
    return r.ok && r.pos == len;   /* exact length per type (ADR 0004) */
}

/* ---- frames ------------------------------------------------------------------------- */

size_t up_frame_build(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out)
{
    out[0] = UP_FRAME_SYNC0;
    out[1] = UP_FRAME_SYNC1;
    out[2] = type;
    out[3] = len;
    if (len > 0) {
        memcpy(out + 4, payload, len);
    }
    uint16_t crc = up_crc16(out + 2, (size_t)len + 2);
    out[4 + len] = (uint8_t)crc;
    out[5 + len] = (uint8_t)(crc >> 8);
    return (size_t)len + UP_FRAME_OVERHEAD;
}

size_t up_msg_frame(const up_msg_t *m, uint8_t *out)
{
    uint8_t payload[UP_PAYLOAD_MAX];
    int len = up_msg_encode(m, payload);
    if (len < 0 || len > UP_PAYLOAD_MAX) {
        return 0;
    }
    return up_frame_build(m->type, payload, (uint8_t)len, out);
}

enum { PS_SYNC0, PS_SYNC1, PS_TYPE, PS_LEN, PS_DATA };

void up_parser_init(up_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

bool up_parser_feed(up_parser_t *p, uint8_t byte, uint32_t now_ms)
{
    if (p->state != PS_SYNC0 && now_ms - p->last_ms > UP_FRAME_GAP_MS) {
        p->state = PS_SYNC0;   /* pause inside a frame: drop it */
    }
    p->last_ms = now_ms;

    switch (p->state) {
    case PS_SYNC0:
        if (byte == UP_FRAME_SYNC0) {
            p->state = PS_SYNC1;
        }
        return false;
    case PS_SYNC1:
        if (byte == UP_FRAME_SYNC1) {
            p->state = PS_TYPE;
        } else if (byte != UP_FRAME_SYNC0) {
            p->state = PS_SYNC0;
        }
        return false;
    case PS_TYPE:
        p->type = byte;
        p->state = PS_LEN;
        return false;
    case PS_LEN:
        p->len = byte;
        p->pos = 0;
        p->state = PS_DATA;
        return false;
    case PS_DATA:
        p->buf[p->pos++] = byte;
        if (p->pos < (uint16_t)p->len + 2) {
            return false;
        }
        p->state = PS_SYNC0;
        {
            uint8_t head[2] = {p->type, p->len};
            uint16_t crc = crc16_update(up_crc16(head, 2), p->buf, p->len);
            uint16_t got = (uint16_t)(p->buf[p->len] | (uint16_t)p->buf[p->len + 1] << 8);
            if (crc != got) {
                p->crc_errors++;
                return false;
            }
        }
        p->frames++;
        return true;
    default:
        p->state = PS_SYNC0;
        return false;
    }
}
