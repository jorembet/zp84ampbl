#include "zp_hid.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int zp_find_hidraw(char *out, size_t out_len)
{
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    int found = 0;

    if (!d)
        return ZP_ERR_OPEN;
    while ((e = readdir(d)) != NULL) {
        char path[512], line[256];
        FILE *f;

        if (strncmp(e->d_name, "hidraw", 6) != 0)
            continue;
        snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/uevent",
                 e->d_name);
        f = fopen(path, "r");
        if (!f)
            continue;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "HID_ID=", 7) != 0)
                continue;
            if (strstr(line + 7, "00004084:00004357")) {
                snprintf(out, out_len, "/dev/%s", e->d_name);
                found = 1;
            }
        }
        fclose(f);
        if (found)
            break;
    }
    closedir(d);
    return found ? ZP_OK : ZP_ERR_NODEV;
}

int zp_open(zp_conn *c, const char *path)
{
    c->timeout_ms = 400;
    c->tag = ZP_FRAME_TAG;
    c->last_rx = 0;
    c->last_tx = 0;
    c->fd = open(path, O_RDWR);
    if (c->fd < 0) {
        c->last_error = ZP_ERR_OPEN;
        return ZP_ERR_OPEN;
    }
    c->last_error = ZP_OK;
    return ZP_OK;
}

void zp_close(zp_conn *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}

size_t zp_frame_pack(const uint8_t *payload, size_t len, uint8_t tag,
                     uint8_t *out, size_t out_cap)
{
    if (len > 0xFFFF)
        return 0;
    if (out_cap < len + ZP_HEADER_SIZE)
        return 0;
    out[0] = ZP_FRAME_SYN0;
    out[1] = ZP_FRAME_SYN1;
    out[2] = (uint8_t)(len >> 8);
    out[3] = (uint8_t)(len & 0xFF);
    out[4] = tag;
    if (len)
        memcpy(out + ZP_HEADER_SIZE, payload, len);
    return len + ZP_HEADER_SIZE;
}

int zp_frame_unpack(const uint8_t *frame, size_t avail, uint8_t *payload,
                    size_t *payload_len)
{
    size_t len;

    if (avail < ZP_HEADER_SIZE) {
        *payload_len = 0;
        return ZP_ERR_SHORT;
    }
    if (frame[0] != ZP_FRAME_SYN0 || frame[1] != ZP_FRAME_SYN1) {
        *payload_len = 0;
        return ZP_ERR_SYNC;
    }
    len = ((size_t)frame[2] << 8) | (size_t)frame[3];
    if (avail < len + ZP_HEADER_SIZE) {
        *payload_len = 0;
        return ZP_ERR_SHORT;
    }
    if (len && payload)
        memcpy(payload, frame + ZP_HEADER_SIZE, len);
    *payload_len = len;
    return ZP_OK;
}

int zp_write_report(zp_conn *c, const uint8_t *report)
{
    ssize_t n = write(c->fd, report, ZP_REPORT_SIZE);
    if (n != (ssize_t)ZP_REPORT_SIZE) {
        c->last_error = (n < 0) ? ZP_ERR_IO : ZP_ERR_SHORT;
        return c->last_error;
    }
    c->last_error = ZP_OK;
    return ZP_OK;
}

int zp_read_report(zp_conn *c, uint8_t *report)
{
    struct pollfd p;
    ssize_t n;

    p.fd = c->fd;
    p.events = POLLIN;
    for (;;) {
        int r = poll(&p, 1, c->timeout_ms);
        if (r == 0) {
            c->last_error = ZP_ERR_TIMEOUT;
            return c->last_error;
        }
        if (r < 0) {
            if (errno == EINTR)
                continue;
            c->last_error = ZP_ERR_IO;
            return c->last_error;
        }
        n = read(c->fd, report, ZP_REPORT_SIZE);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            c->last_error = ZP_ERR_IO;
            return c->last_error;
        }
        if (n == 0) {
            c->last_error = ZP_ERR_IO;
            return c->last_error;
        }
        c->last_error = ZP_OK;
        c->last_rx += (uint32_t)n;
        return ZP_OK;
    }
}

int zp_drain(zp_conn *c)
{
    uint8_t buf[ZP_REPORT_SIZE];
    struct pollfd p;
    int drained = 0;

    p.fd = c->fd;
    p.events = POLLIN;
    while (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
        if (read(c->fd, buf, sizeof(buf)) <= 0)
            break;
        drained++;
    }
    return drained;
}

int zp_send(zp_conn *c, const uint8_t *payload, size_t len)
{
    uint8_t frame[ZP_MAX_PAYLOAD + ZP_HEADER_SIZE];
    uint8_t report[ZP_REPORT_SIZE];
    size_t total, off = 0;
    int r;

    if (len + ZP_HEADER_SIZE > sizeof(frame)) {
        c->last_error = ZP_ERR_OVERFLOW;
        return c->last_error;
    }
    total = zp_frame_pack(payload, len, c->tag, frame, sizeof(frame));
    if (total == 0) {
        c->last_error = ZP_ERR_ARG;
        return c->last_error;
    }

    while (off < total) {
        size_t n = total - off;
        if (n > ZP_REPORT_SIZE)
            n = ZP_REPORT_SIZE;
        memset(report, 0, ZP_REPORT_SIZE);
        memcpy(report, frame + off, n);
        r = zp_write_report(c, report);
        if (r != ZP_OK)
            return r;
        c->last_tx += (uint32_t)n;
        off += n;
    }
    return ZP_OK;
}

int zp_recv(zp_conn *c, uint8_t *out, size_t out_cap, size_t *out_len)
{
    uint8_t report[ZP_REPORT_SIZE];
    uint8_t frame[ZP_MAX_PAYLOAD + ZP_HEADER_SIZE];
    size_t have = 0, need = 0, got;
    int r, first = 1;

    for (;;) {
        r = zp_read_report(c, report);
        if (r != ZP_OK)
            return r;

        if (first) {
            if (report[0] != ZP_FRAME_SYN0 || report[1] != ZP_FRAME_SYN1) {
                if (++first > 8) {
                    c->last_error = ZP_ERR_SYNC;
                    return c->last_error;
                }
                continue;
            }
            first = 0;
            if (report[4] != c->tag)
                c->tag = report[4];
            need = ((size_t)report[2] << 8) | (size_t)report[3];
            if (need + ZP_HEADER_SIZE > sizeof(frame)) {
                c->last_error = ZP_ERR_OVERFLOW;
                return c->last_error;
            }
        }

        if (have < sizeof(frame))
            memcpy(frame + have, report, ZP_REPORT_SIZE);
        have += ZP_REPORT_SIZE;

        if (have >= need + ZP_HEADER_SIZE)
            break;
    }

    got = need;
    if (got > out_cap) {
        c->last_error = ZP_ERR_OVERFLOW;
        return c->last_error;
    }
    if (got)
        memcpy(out, frame + ZP_HEADER_SIZE, got);
    *out_len = got;
    c->last_error = ZP_OK;
    return ZP_OK;
}

int zp_xfer(zp_conn *c, const uint8_t *payload, size_t len, uint8_t *out,
            size_t out_cap, size_t *out_len)
{
    int r;

    zp_drain(c);
    r = zp_send(c, payload, len);
    if (r != ZP_OK)
        return r;
    return zp_recv(c, out, out_cap, out_len);
}

const char *zp_strerror(uint32_t err)
{
    switch (err) {
    case ZP_OK:
        return "ok";
    case ZP_ERR_OPEN:
        return "cannot open hidraw (permission denied?)";
    case ZP_ERR_TIMEOUT:
        return "timeout - no reply from device";
    case ZP_ERR_SHORT:
        return "short transfer";
    case ZP_ERR_NODEV:
        return "device not found";
    case ZP_ERR_IO:
        return "I/O error";
    case ZP_ERR_ARG:
        return "bad argument";
    case ZP_ERR_SYNC:
        return "bad frame sync (no AE 1E header)";
    case ZP_ERR_OVERFLOW:
        return "payload too large";
    default:
        return "unknown";
    }
}
