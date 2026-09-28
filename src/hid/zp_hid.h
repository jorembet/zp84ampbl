#ifndef ZP_HID_H
#define ZP_HID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZP_USB_VID 0x4084
#define ZP_USB_PID 0x4357

#define ZP_REPORT_SIZE 64
#define ZP_HEADER_SIZE 5

#define ZP_FRAME_SYN0 0xAE
#define ZP_FRAME_SYN1 0x1E
#define ZP_FRAME_TAG 0xC8

#define ZP_MAX_PAYLOAD 1024

enum {
    ZP_OK = 0,
    ZP_ERR_OPEN = 1,
    ZP_ERR_TIMEOUT = 2,
    ZP_ERR_SHORT = 3,
    ZP_ERR_NODEV = 4,
    ZP_ERR_IO = 5,
    ZP_ERR_ARG = 6,
    ZP_ERR_SYNC = 7,
    ZP_ERR_OVERFLOW = 8
};

typedef struct {
    int fd;
    int timeout_ms;
    uint8_t tag;
    uint32_t last_error;
    uint32_t last_rx;
    uint32_t last_tx;
} zp_conn;

int zp_find_hidraw(char *out, size_t out_len);
int zp_open(zp_conn *c, const char *path);
void zp_close(zp_conn *c);

size_t zp_frame_pack(const uint8_t *payload, size_t len, uint8_t tag,
                     uint8_t *out, size_t out_cap);
int zp_frame_unpack(const uint8_t *frame, size_t avail, uint8_t *payload,
                    size_t *payload_len);

int zp_send(zp_conn *c, const uint8_t *payload, size_t len);
int zp_recv(zp_conn *c, uint8_t *out, size_t out_cap, size_t *out_len);
int zp_xfer(zp_conn *c, const uint8_t *payload, size_t len, uint8_t *out,
            size_t out_cap, size_t *out_len);

int zp_write_report(zp_conn *c, const uint8_t *report);
int zp_read_report(zp_conn *c, uint8_t *report);
int zp_drain(zp_conn *c);

const char *zp_strerror(uint32_t err);

#ifdef __cplusplus
}
#endif

#endif
