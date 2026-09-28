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

    {
        static const uint8_t v1[] = "123456789";
        check("CRC-16/MODBUS check value for \"123456789\" is 0x4B37",
              zp_crc16_modbus(v1, 9) == 0x4B37);
        check("CRC-16/MODBUS of empty input is 0xFFFF",
              zp_crc16_modbus(v1, 0) == 0xFFFF);
        {
            static const uint8_t v2[] = {0x00};
            check("CRC-16/MODBUS of 0x00 is 0x40BF",
                  zp_crc16_modbus(v2, 1) == 0x40BF);
        }
        {
            static const uint8_t v3[] = {0x01, 0x02, 0x03, 0x04};
            check("CRC changes with input",
                  zp_crc16_modbus(v3, 4) != zp_crc16_modbus(v3, 3));
        }
    }

    {
        uint8_t inner[128], out2[128], cmd_out = 0;
        size_t ilen, plen2 = 0;

        for (i = 0; i < 16; i++)
            payload[i] = (uint8_t)(0xF0 + i);
        ilen = zp_inner_pack(payload, 16, 0x06, inner, sizeof(inner));
        check("inner frame total is payload + 5", ilen == 16 + 5);
        check("inner byte 0 is 0x80", inner[0] == 0x80);
        check("inner byte 1 is payload + 3", inner[1] == 16 + 3);
        check("inner byte 2 is the command", inner[2] == 0x06);
        check("inner payload follows the 3 byte header",
              memcmp(inner + 3, payload, 16) == 0);
        {
            uint16_t want = zp_crc16_modbus(inner, 16 + 3);
            check("inner CRC high byte", inner[16 + 3] == (uint8_t)(want >> 8));
            check("inner CRC low byte", inner[16 + 4] == (uint8_t)(want & 0xFF));
        }
        check("inner round trip",
              zp_inner_unpack(inner, ilen, &cmd_out, out2, sizeof(out2),
                              &plen2) == ZP_OK);
        check("inner round trip length", plen2 == 16);
        check("inner round trip command", cmd_out == 0x06);
        check("inner round trip payload", memcmp(out2, payload, 16) == 0);

        inner[10] ^= 0x01;
        check("corrupt payload fails CRC",
              zp_inner_unpack(inner, ilen, &cmd_out, out2, sizeof(out2),
                              &plen2) == ZP_ERR_CRC);
        inner[10] ^= 0x01;

        check("oversized destination rejected",
              zp_inner_unpack(inner, ilen, &cmd_out, out2, 4, &plen2) ==
                  ZP_ERR_OVERFLOW);
        check("inner header 0x80 required",
              zp_inner_unpack((const uint8_t *)"\x81\x05\x06", 5, &cmd_out,
                              out2, sizeof(out2), &plen2) == ZP_ERR_SYNC);
        check("0xFF length sentinel rejected",
              zp_inner_unpack((const uint8_t *)"\x80\xff\x06", 5, &cmd_out,
                              out2, sizeof(out2), &plen2) == ZP_ERR_OVERFLOW);
    }

    {
        static const uint8_t rec[] = {0x00, 0x00, 0x20, 0x23,   /* id 0, type 0x2023 */
                                     0x00, 0x01, 0x17, 0x02,   /* id 1, type 0x1702 */
                                     0x00, 0x02, 0xf4, 0x01};  /* id 2, type 0xf401 */
        zp_param tab[8];
        zp_param got;
        size_t n = 0;

        check("record parse succeeds", zp_params_parse(rec, sizeof(rec), tab, 8, &n) == 0);
        check("three records parsed", n == 3);
        check("id 0 present", zp_param_get(tab, ZP_PARAM_COUNT, 0, &got) == 1);
        check("id 0 type is 0x2023", got.type == 0x2023);
        check("id 1 type is 0x1702", zp_param_get(tab, ZP_PARAM_COUNT, 1, &got) == 1 && got.type == 0x1702);
        check("id 2 type is 0xf401", zp_param_get(tab, ZP_PARAM_COUNT, 2, &got) == 1 && got.type == 0xf401);
        check("undefined id 7 absent", zp_param_get(tab, ZP_PARAM_COUNT, 7, &got) == 0);
        check("out of range id rejected", zp_param_get(tab, ZP_PARAM_COUNT, 0xFFFF, &got) == 0);

        {
            static const uint8_t big[] = {0x0F, 0x8E, 0xAB, 0xCD};
            check("id 0x0F8E is past the bound and skipped", zp_params_parse(big, 4, tab, 8, &n) == 0);
            check("out of bound record not counted", n == 0);
        }
    }

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
