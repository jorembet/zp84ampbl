#include "../src/hid/zp_hid.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    puts("zpsniff - ZP 8.4 AMP (Nuvoton 4084:4357) frame explorer\n"
         "\n"
         "Wire format (reverse engineered from the vendor PC tool):\n"
         "    AE 1E <len_hi> <len_lo> <tag> <payload...>\n"
         "len is 16-bit big endian and counts payload bytes only; the whole\n"
         "frame is split into 64-byte HID reports.\n"
         "\n"
         "usage: zpsniff <cmd> [options]\n"
         "\n"
         "  find                       locate the hidraw node\n"
         "  ping                       send an empty frame, report any reply\n"
         "  send   -i FILE             send FILE as the payload\n"
         "  xfer   -i FILE             send FILE, print the reply payload\n"
         "  raw    -i FILE [-o FILE]   send FILE verbatim (no framing)\n"
         "\n"
         "  --dev PATH                 override hidraw node\n"
         "  --tag N                    frame tag byte (default 0xC8)\n"
         "  --tmo MS                   reply timeout (default 400)\n"
         "  --hex STR                  inline payload as hex bytes\n"
         "  --keep-sync                do not resync on a bad AE 1E header");
}

static void hexdump(FILE *f, const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i += 16) {
        size_t j;
        fprintf(f, "%04zx  ", i);
        for (j = 0; j < 16; j++) {
            if (i + j < n)
                fprintf(f, "%02x ", b[i + j]);
            else
                fputs("   ", f);
        }
        fprintf(f, " |");
        for (j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = b[i + j];
            fputc((c >= 32 && c < 127) ? c : '.', f);
        }
        fputs("|\n", f);
    }
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > ZP_MAX_PAYLOAD) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static int parse_hex(const char *s, uint8_t *out, size_t cap, size_t *len)
{
    size_t n = 0;
    while (*s) {
        unsigned v;
        while (*s == ' ' || *s == ':' || *s == ',')
            s++;
        if (!*s)
            break;
        if (sscanf(s, "%2x", &v) != 1)
            return -1;
        if (n >= cap)
            return -1;
        out[n++] = (uint8_t)v;
        s += 2;
    }
    *len = n;
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = NULL, *dev = NULL, *in_path = NULL, *out_path = NULL;
    const char *hex = NULL;
    uint32_t tag = ZP_FRAME_TAG;
    int tmo = 400, i, keep_sync = 0;
    uint8_t payload[ZP_MAX_PAYLOAD], reply[ZP_MAX_PAYLOAD];
    size_t plen = 0, rlen = 0;
    zp_conn c;
    int rc = 0;

    if (argc < 2) {
        usage();
        return 1;
    }
    cmd = argv[1];

    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        int has_next = (i + 1 < argc);
        if (!strcmp(a, "--dev") && has_next)
            dev = argv[++i];
        else if (!strcmp(a, "--tag") && has_next)
            tag = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--tmo") && has_next)
            tmo = atoi(argv[++i]);
        else if (!strcmp(a, "-i") && has_next)
            in_path = argv[++i];
        else if (!strcmp(a, "-o") && has_next)
            out_path = argv[++i];
        else if (!strcmp(a, "--hex") && has_next)
            hex = argv[++i];
        else if (!strcmp(a, "--keep-sync"))
            keep_sync = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            return 1;
        }
    }
    (void)keep_sync;

    if (!strcmp(cmd, "find")) {
        char path[256];
        if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
            puts("not found");
            return 1;
        }
        puts(path);
        return 0;
    }

    if (strcmp(cmd, "ping") && strcmp(cmd, "send") && strcmp(cmd, "xfer") &&
        strcmp(cmd, "raw")) {
        usage();
        return 1;
    }

    if (hex) {
        if (parse_hex(hex, payload, sizeof(payload), &plen) != 0) {
            fprintf(stderr, "bad --hex value\n");
            return 1;
        }
    } else if (in_path) {
        uint8_t *buf = read_file(in_path, &plen);
        if (!buf) {
            fprintf(stderr, "cannot read %s\n", in_path);
            return 1;
        }
        memcpy(payload, buf, plen);
        free(buf);
    }

    memset(&c, 0, sizeof(c));
    c.fd = -1;
    {
        char path[256];
        if (!dev) {
            if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
                fprintf(stderr, "device %04x:%04x not found\n", ZP_USB_VID,
                        ZP_USB_PID);
                return 1;
            }
            dev = path;
            printf("node: %s\n", dev);
        }
        if (zp_open(&c, dev) != ZP_OK) {
            fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
            return 1;
        }
    }
    c.tag = (uint8_t)tag;
    c.timeout_ms = tmo;

    if (!strcmp(cmd, "ping")) {
        int r = zp_xfer(&c, payload, plen, reply, sizeof(reply), &rlen);
        if (r != ZP_OK) {
            printf("no reply: %s\n", zp_strerror((uint32_t)r));
            rc = 1;
        } else {
            printf("reply %zu byte(s)\n", rlen);
            hexdump(stdout, reply, rlen);
        }
    } else if (!strcmp(cmd, "send")) {
        int r = zp_send(&c, payload, plen);
        printf("sent %zu payload byte(s) as a %zu byte frame: %s\n", plen,
               plen + ZP_HEADER_SIZE, zp_strerror((uint32_t)r));
        rc = r != ZP_OK;
    } else if (!strcmp(cmd, "xfer")) {
        int r = zp_xfer(&c, payload, plen, reply, sizeof(reply), &rlen);
        if (r != ZP_OK) {
            printf("no reply: %s\n", zp_strerror((uint32_t)r));
            rc = 1;
        } else {
            printf("reply %zu byte(s)\n", rlen);
            hexdump(stdout, reply, rlen);
            if (out_path) {
                FILE *f = fopen(out_path, "wb");
                if (f) {
                    fwrite(reply, 1, rlen, f);
                    fclose(f);
                }
            }
        }
    } else {
        int r = 0;
        size_t off = 0;
        printf("sending %zu raw byte(s) with no framing\n", plen);
        while (off < plen) {
            uint8_t rep[ZP_REPORT_SIZE];
            size_t n = plen - off;
            if (n > ZP_REPORT_SIZE)
                n = ZP_REPORT_SIZE;
            memset(rep, 0, ZP_REPORT_SIZE);
            memcpy(rep, payload + off, n);
            r = zp_write_report(&c, rep);
            if (r != ZP_OK)
                break;
            off += n;
        }
        printf("done: %s\n", zp_strerror((uint32_t)r));
        rc = r != ZP_OK;
    }

    zp_close(&c);
    return rc;
}
