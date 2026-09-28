#include "../src/hid/zp_hid.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void check(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s\n", what);
    } else {
        printf("ok   %s\n", what);
    }
}

int main(void)
{
    uint8_t frame[512], out[512], payload[256];
    size_t n, got = 0;
    int i;

    memset(payload, 0x11, sizeof(payload));

    n = zp_frame_pack(payload, 3, ZP_FRAME_TAG, frame, sizeof(frame));
    check("frame length is header + payload", n == ZP_HEADER_SIZE + 3);
    check("sync byte 0", frame[0] == 0xAE);
    check("sync byte 1", frame[1] == 0x1E);
    check("len high byte", frame[2] == 0x00);
    check("len low byte", frame[3] == 0x03);
    check("tag byte", frame[4] == 0xC8);
    check("payload follows header", memcmp(frame + 5, payload, 3) == 0);

    check("unpack round trip", zp_frame_unpack(frame, n, out, &got) == ZP_OK);
    check("unpack length", got == 3);
    check("unpack payload", memcmp(out, payload, 3) == 0);

    n = zp_frame_pack(payload, 258, ZP_FRAME_TAG, frame, sizeof(frame));
    check("big-endian len high byte", frame[2] == 0x01);
    check("big-endian len low byte", frame[3] == 0x02);
    got = 0;
    check("258 byte round trip", zp_frame_unpack(frame, n, out, &got) == ZP_OK);
    check("258 byte length preserved", got == 258);

    frame[0] = 0x00;
    check("bad sync rejected", zp_frame_unpack(frame, 8, out, &got) == ZP_ERR_SYNC);
    frame[0] = 0xAE;
    frame[1] = 0x1E;
    check("truncated frame rejected", zp_frame_unpack(frame, 4, out, &got) == ZP_ERR_SHORT);
    check("short header rejected", zp_frame_unpack(frame, 2, out, &got) == ZP_ERR_SHORT);

    n = zp_frame_pack(payload, 10, 0x00, frame, sizeof(frame));
    check("custom tag accepted", frame[4] == 0x00);

    check("insufficient output rejected", zp_frame_pack(payload, 4, ZP_FRAME_TAG, frame, 8) == 0);

    n = zp_frame_pack(payload, 64, ZP_FRAME_TAG, frame, sizeof(frame));
    check("full 64 byte payload frames at 69", n == ZP_HEADER_SIZE + 64);
    check("over 64 byte payload spans two reports", n > ZP_REPORT_SIZE);

    for (i = 0; i < 256; i++)
        payload[i] = (uint8_t)i;
    n = zp_frame_pack(payload, 59, ZP_FRAME_TAG, frame, sizeof(frame));
    check("59 byte payload frames at 64 (one report)", n == 64);

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
