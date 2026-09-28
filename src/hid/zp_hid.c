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

/* CRC-16/MODBUS: poly 0x8005 reflected (0xA001), init 0xFFFF,
   refin/refout true, xorout 0. Check value for "123456789" is 0x4B37. */
uint16_t zp_crc16_modbus(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (b = 0; b < 8; b++) {
            if (crc & 1)
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            else
                crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

size_t zp_inner_pack(const uint8_t *payload, size_t len, uint8_t cmd,
                     uint8_t *out, size_t out_cap)
{
    uint16_t crc;
    size_t n;

    if (out_cap < len + ZP_INNER_OVERHEAD)
        return 0;
    out[0] = 0x80;
    if (len >= ZP_INNER_LEN_MAX)
        out[1] = 0xFF;
    else
        out[1] = (uint8_t)(len + ZP_INNER_HDR);
    out[2] = cmd;
    if (len)
        memcpy(out + ZP_INNER_HDR, payload, len);
    n = len + ZP_INNER_HDR;
    crc = zp_crc16_modbus(out, n);
    out[n] = (uint8_t)(crc >> 8);
    out[n + 1] = (uint8_t)(crc & 0xFF);
    return n + ZP_INNER_CRC;
}

int zp_inner_unpack(const uint8_t *in, size_t avail, uint8_t *cmd,
                    uint8_t *payload, size_t out_cap, size_t *payload_len)
{
    size_t n;

    if (avail < ZP_INNER_OVERHEAD) {
        *payload_len = 0;
        return ZP_ERR_SHORT;
    }
    if (in[0] != 0x80) {
        *payload_len = 0;
        return ZP_ERR_SYNC;
    }
    if (in[1] == 0xFF) {
        *payload_len = 0;
        return ZP_ERR_OVERFLOW;
    }
    n = (size_t)in[1];
    if (n < ZP_INNER_HDR) {
        *payload_len = 0;
        return ZP_ERR_SHORT;
    }
    if (avail < n + ZP_INNER_CRC) {
        *payload_len = 0;
        return ZP_ERR_SHORT;
    }
    {
        uint16_t want = zp_crc16_modbus(in, n);
        uint16_t got = (uint16_t)((in[n] << 8) | in[n + 1]);
        if (want != got) {
            *payload_len = 0;
            return ZP_ERR_CRC;
        }
    }
    if (cmd)
        *cmd = in[2];
    n -= ZP_INNER_HDR;
    if (payload && n > out_cap) {
        *payload_len = 0;
        return ZP_ERR_OVERFLOW;
    }
    if (n && payload)
        memcpy(payload, in + ZP_INNER_HDR, n);
    *payload_len = n;
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

int zp_send(zp_conn *c, const uint8_t *payload, size_t len, uint8_t cmd)
{
    uint8_t inner[ZP_MAX_PAYLOAD + ZP_INNER_OVERHEAD];
    uint8_t frame[ZP_MAX_PAYLOAD + ZP_INNER_OVERHEAD + ZP_HEADER_SIZE];
    uint8_t report[ZP_REPORT_SIZE];
    size_t inner_len, total, off = 0;
    int r;

    inner_len = zp_inner_pack(payload, len, cmd, inner, sizeof(inner));
    if (inner_len == 0) {
        c->last_error = ZP_ERR_ARG;
        return c->last_error;
    }
    if (inner_len + ZP_HEADER_SIZE > sizeof(frame)) {
        c->last_error = ZP_ERR_OVERFLOW;
        return c->last_error;
    }
    total = zp_frame_pack(inner, inner_len, c->tag, frame, sizeof(frame));
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

int zp_recv(zp_conn *c, uint8_t *cmd, uint8_t *out, size_t out_cap,
            size_t *out_len)
{
    uint8_t report[ZP_REPORT_SIZE];
    uint8_t frame[ZP_MAX_PAYLOAD + ZP_INNER_OVERHEAD + ZP_HEADER_SIZE];
    size_t have = 0, need = 0;
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

    return zp_inner_unpack(frame + ZP_HEADER_SIZE, need, cmd, out, out_cap,
                           out_len);
}

int zp_xfer(zp_conn *c, const uint8_t *payload, size_t len, uint8_t cmd,
            uint8_t *rsp_cmd, uint8_t *out, size_t out_cap, size_t *out_len)
{
    uint8_t local = 0;
    uint8_t *slot = rsp_cmd ? rsp_cmd : &local;
    int r;

    zp_drain(c);
    r = zp_send(c, payload, len, cmd);
    if (r != ZP_OK)
        return r;
    return zp_recv(c, slot, out, out_cap, out_len);
}

void zp_block_request(uint16_t addr, uint8_t *payload)
{
    payload[0] = (uint8_t)((addr >> 8) & 0xFF);
    payload[1] = (uint8_t)(addr & 0xFF);
    payload[2] = 0;
}

int zp_block_read(zp_conn *c, uint16_t addr, uint8_t *out)
{
    uint8_t req[ZP_BLOCK_ADDR_BYTES];
    uint8_t reply[ZP_MAX_PAYLOAD];
    size_t rlen = 0;
    int r;

    zp_block_request(addr, req);
    r = zp_xfer(c, req, sizeof(req), ZP_CMD_BLOCK, NULL, reply,
                sizeof(reply), &rlen);
    if (r != ZP_OK)
        return r;
    if (rlen < ZP_BLOCK_SIZE) {
        c->last_error = ZP_ERR_SHORT;
        return c->last_error;
    }
    memcpy(out, reply, ZP_BLOCK_SIZE);
    return ZP_OK;
}

int zp_block_write(zp_conn *c, uint16_t addr, const uint8_t *data, size_t len)
{
    uint8_t payload[ZP_BLOCK_ADDR_BYTES + ZP_BLOCK_SIZE];

    if (len > ZP_BLOCK_SIZE) {
        c->last_error = ZP_ERR_ARG;
        return c->last_error;
    }
    zp_block_request(addr, payload);
    memcpy(payload + ZP_BLOCK_ADDR_BYTES, data, len);
    if (len < ZP_BLOCK_SIZE)
        memset(payload + ZP_BLOCK_ADDR_BYTES + len, 0,
               ZP_BLOCK_SIZE - len);
    return zp_send(c, payload, sizeof(payload), ZP_CMD_BLOCK);
}

int zp_byte_write(zp_conn *c, uint8_t value)
{
    return zp_send(c, &value, 1, ZP_CMD_BYTE_WRITE);
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
    case ZP_ERR_CRC:
        return "CRC mismatch";
    default:
        return "unknown";
    }
}
