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

#define ZP_INNER_HDR 3
#define ZP_INNER_CRC 2
#define ZP_INNER_OVERHEAD (ZP_INNER_HDR + ZP_INNER_CRC)
#define ZP_INNER_LEN_MAX 250

/* CMD_BLOCK reads or writes a 256 byte page at a 24-bit address.
   payload = addr[3] big endian, then 256 bytes. total 259.
   (For the 16-bit addresses used here the first byte is 0.) */
#define ZP_CMD_BLOCK 0xFF
#define ZP_BLOCK_SIZE 256
#define ZP_BLOCK_ADDR_BYTES 3
#define ZP_MAX_ADDR 0xFFFF

/* The full parameter image the application streams with CMD_BYTE_WRITE. */
#define ZP_PARAM_BYTES 0x78E
#define ZP_PARAM_COUNT ZP_PARAM_BYTES
#define ZP_PARAM_STRIDE 24

typedef struct {
    uint16_t type;
    uint8_t flag;
    uint32_t value;
} zp_param;

/* Parse 4 byte big endian records (id_hi id_lo type_hi type_lo) as the
   vendor tool writes them, filling a parameter table. */
int zp_params_parse(const uint8_t *rec, size_t len, zp_param *out, size_t cap,
                    size_t *count);
int zp_param_get(const zp_param *tab, size_t count, uint16_t id,
                 zp_param *out);

/* command bytes seen in the vendor tool */
#define ZP_CMD_00 0x00
#define ZP_CMD_03 0x03
#define ZP_CMD_04 0x04
#define ZP_CMD_06 0x06
#define ZP_CMD_10 0x10
#define ZP_CMD_20 0x20
#define ZP_CMD_21 0x21
#define ZP_CMD_57 0x57
#define ZP_CMD_5A 0x5A
#define ZP_CMD_5B 0x5B
#define ZP_CMD_5C 0x5C
#define ZP_CMD_F0 0xF0
#define ZP_CMD_FC 0xFC
#define ZP_CMD_FE 0xFE
#define ZP_CMD_FF 0xFF

/* CMD_BYTE_WRITE: 1 byte payload, streamed 0x78E times to load the
   complete parameter image. */
#define ZP_CMD_BYTE_WRITE 0x04
#define ZP_CMD_ID 0x06

enum {
    ZP_OK = 0,
    ZP_ERR_OPEN = 1,
    ZP_ERR_TIMEOUT = 2,
    ZP_ERR_SHORT = 3,
    ZP_ERR_NODEV = 4,
    ZP_ERR_IO = 5,
    ZP_ERR_ARG = 6,
    ZP_ERR_SYNC = 7,
    ZP_ERR_OVERFLOW = 8,
    ZP_ERR_CRC = 9
};

typedef struct zp_conn {
    int fd;
    int timeout_ms;
    uint8_t tag;
    uint32_t last_error;
    uint32_t last_rx;
    uint32_t last_tx;
    /* errno from the last failed open/read/write (0 if none/not applicable),
       so callers can print the real reason with strerror(). */
    int last_errno;
    int (*custom_xfer)(struct zp_conn *,const uint8_t *,size_t,uint8_t,uint8_t *,uint8_t *,size_t,size_t *);
    void (*custom_close)(struct zp_conn *);
} zp_conn;

int zp_find_hidraw(char *out, size_t out_len);
int zp_open(zp_conn *c, const char *path);
void zp_close(zp_conn *c);

size_t zp_frame_pack(const uint8_t *payload, size_t len, uint8_t tag,
                     uint8_t *out, size_t out_cap);
int zp_frame_unpack(const uint8_t *frame, size_t avail, uint8_t *payload,
                    size_t *payload_len);

uint16_t zp_crc16_modbus(const uint8_t *buf, size_t len);
size_t zp_inner_pack(const uint8_t *payload, size_t len, uint8_t cmd,
                     uint8_t *out, size_t out_cap);
/* Unpack one inner frame. When the length byte is 0xFF (payloads >= 250
   bytes) the true size comes from the outer HID frame, so avail must be
   exactly the outer payload length (L = payload + 5), which is what
   zp_recv passes. */
int zp_inner_unpack(const uint8_t *in, size_t avail, uint8_t *cmd,
                    uint8_t *payload, size_t out_cap, size_t *payload_len);

int zp_send(zp_conn *c, const uint8_t *payload, size_t len, uint8_t cmd);
int zp_recv(zp_conn *c, uint8_t *cmd, uint8_t *out, size_t out_cap,
            size_t *out_len);
int zp_xfer(zp_conn *c, const uint8_t *payload, size_t len, uint8_t cmd,
            uint8_t *rsp_cmd, uint8_t *out, size_t out_cap, size_t *out_len);

void zp_block_request(uint16_t addr, uint8_t *payload);
int zp_block_read(zp_conn *c, uint16_t addr, uint8_t *out);
int zp_block_write(zp_conn *c, uint16_t addr, const uint8_t *data,
                   size_t len);
int zp_byte_write(zp_conn *c, uint8_t value);

/* CMD_ID takes a list of 16 bit parameter ids, big endian on the wire,
   and returns one 4 byte big endian (id, value) record per id, in order.
   Confirmed on hardware and against the vendor tool's reply handler
   (0x41f342), which reads both halves big endian. */
#define ZP_ID_BYTES 2
#define ZP_ID_VALUE_BYTES 4
#define ZP_ID_RECORD_BYTES 4
int zp_id_query(zp_conn *c, const uint16_t *ids, size_t count, uint8_t *out,
                size_t out_cap, size_t *out_len);

/* Write one parameter using cmd 03; require echo acknowledgement and readback. */
int zp_id_write(zp_conn *c, uint16_t id, uint16_t value);

int zp_write_report(zp_conn *c, const uint8_t *report);
int zp_read_report(zp_conn *c, uint8_t *report);
int zp_drain(zp_conn *c);

const char *zp_strerror(uint32_t err);

#ifdef __cplusplus
}
#endif

#endif
