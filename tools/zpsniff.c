#define _POSIX_C_SOURCE 200809L
#include "../src/hid/zp_hid.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    puts("zpsniff - ZP 8.4 AMP (Nuvoton 4084:4357) frame explorer\n"
         "\n"
         "Wire format, reverse engineered from the vendor PC tool:\n"
         "    report (64B) -> AE 1E <len:16 BE> C8 <inner>\n"
         "    inner         -> 80 <len+3 or 0xFF> <cmd> <payload> <CRC16 modbus>\n"
         "\n"
         "usage: zpsniff <cmd> [options]\n"
         "\n"
         "  find                          locate the hidraw node (checks access)\n"
         "  ping                          send the vendor 0x06 handshake\n"
         "  sweep                         try 0x06 with payload lengths 0..8\n"
         "  get    --id A [--id B ...]    read parameters by 16 bit id\n"
         "  snap   -o FILE [--batch B]    read all ids (default batch 8)\n"
         "  diff   A B                     compare two snapshots (offline)\n"
         "  idscan --from A --count N [--batch B]\n"
         "                                scan the id space, summarise\n"
         "  idscan ... -v                  also list every non-zero id\n"
         "  send   -i FILE                send FILE as the payload\n"
         "  xfer   -i FILE                send FILE, print the reply\n"
         "  raw    -i FILE                send FILE verbatim, no framing\n"
         "  block  --addr A [-o FILE]     read a 256 byte page at address A\n"
         "  dump   -o FILE                read the whole 0x78E byte image\n"
         "\n"
         "  --dev PATH                    override hidraw node\n"
         "  --cmd N                       command byte (default 0x06)\n"
         "  --tag N                       frame tag byte (default 0xC8)\n"
         "  --tmo MS                      reply timeout (default 400)\n"
         "  --hex STR                     inline payload as hex bytes");
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
    uint32_t tag = ZP_FRAME_TAG, addr = 0, id_from = 0, id_count = 0;
    uint32_t id_batch = 0;
    int verbose = 0;
    uint16_t ids[64];
    size_t nids = 0;
    uint32_t cmdid = 0x06;
    uint8_t rspcmd = 0;
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

    /* diff takes two positional files and needs no device or options. */
    if (!strcmp(cmd, "diff")) {
        FILE *fa, *fb;
        if (argc < 4) {
            fprintf(stderr, "usage: zpsniff diff <a.txt> <b.txt>\n");
            return 1;
        }
        fa = fopen(argv[2], "r");
        fb = fopen(argv[3], "r");
        if (!fa || !fb) {
            fprintf(stderr, "cannot open both snapshots\n");
            if (fa)
                fclose(fa);
            if (fb)
                fclose(fb);
            return 1;
        }
        {
            char la[128], lb[128];
            int changed = 0;
            while (fgets(la, sizeof(la), fa) && fgets(lb, sizeof(lb), fb)) {
                unsigned ida, idb, va, vb, ta, tb;
                if (sscanf(la, "%x %x %x", &ida, &ta, &va) != 3)
                    continue;
                if (sscanf(lb, "%x %x %x", &idb, &tb, &vb) != 3)
                    continue;
                if (va != vb || ta != tb) {
                    printf("id 0x%04x: type 0x%04x->0x%04x  value 0x%04x->0x%04x\n",
                           ida, ta, tb, va, vb);
                    changed++;
                }
            }
            if (!changed)
                printf("no differences\n");
            else
                printf("\n%d id(s) changed\n", changed);
        }
        fclose(fa);
        fclose(fb);
        return 0;
    }

    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        int has_next = (i + 1 < argc);
        if (!strcmp(a, "--dev") && has_next)
            dev = argv[++i];
        else if (!strcmp(a, "--cmd") && has_next)
            cmdid = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--id") && has_next) {
            if (nids < 64)
                ids[nids++] = (uint16_t)strtoul(argv[++i], NULL, 0);
            else
                i++;
        } else if (!strcmp(a, "--ids") && has_next) {
            const char *p2 = argv[++i];
            while (*p2 && nids < 64) {
                ids[nids++] = (uint16_t)strtoul(p2, (char **)&p2, 0);
                while (*p2 == ',' || *p2 == ' ')
                    p2++;
            }
        } else if (!strcmp(a, "--from") && has_next)
            id_from = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "-v"))
            verbose = 1;
        else if (!strcmp(a, "--batch") && has_next)
            id_batch = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--count") && has_next)
            id_count = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--addr") && has_next)
            addr = (uint32_t)strtoul(argv[++i], NULL, 0);
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
        int usable = 1;
        if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
            puts("not found (no 4084:4357 hidraw node)");
            return 1;
        }
        printf("node: %s\n", path);
        if (access(path, R_OK | W_OK) == 0) {
            puts("access: read/write OK");
        } else {
            printf("access: %s -- fix with one of:\n"
                   "  sudo ./scripts/install-udev.sh   (persistent)\n"
                   "  sudo ./build/zpsniff ping        (quick try as root)\n",
                   strerror(errno));
            usable = 0;
        }
        if (access("/dev/zp84amp", F_OK) == 0)
            puts("symlink: /dev/zp84amp present");
        else
            puts("symlink: /dev/zp84amp missing (udev rule not installed?)");
        return usable ? 0 : 1;
    }

    if (strcmp(cmd, "get") && strcmp(cmd, "snap") && strcmp(cmd, "diff") &&
        strcmp(cmd, "idscan") && strcmp(cmd, "ping") &&
        strcmp(cmd, "sweep") && strcmp(cmd, "send") &&
        strcmp(cmd, "xfer") && strcmp(cmd, "raw") && strcmp(cmd, "block") &&
        strcmp(cmd, "dump")) {
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
        (void)0;
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
            int e = c.last_errno ? c.last_errno : errno;
            fprintf(stderr, "open %s: %s\n", dev, strerror(e));
            if (e == EACCES)
                fprintf(stderr,
                        "hint: no read/write access to the hidraw node.\n"
                        "  sudo ./scripts/install-udev.sh   (persistent)\n"
                        "  id -nG   (must include plugdev, then relogin)\n"
                        "  sudo ./build/zpsniff ping        (quick try as root)\n");
            else if (e == ENOENT)
                fprintf(stderr,
                        "hint: node is gone, the amp probably re-enumerated.\n"
                        "  ./build/zpsniff find   (get the current node)\n"
                        "  dmesg | tail           (check USB disconnects)\n");
            return 1;
        }
    }
    c.tag = (uint8_t)tag;
    c.timeout_ms = tmo;

    if (!strcmp(cmd, "snap")) {
        FILE *f;
        uint32_t base, batch = id_batch ? id_batch : 8;
        const char *dest = out_path ? out_path : "snapshot.txt";
        char *temp = malloc(strlen(dest) + sizeof(".XXXXXX"));
        int fd;
        if (!temp || batch > 64) {
            fprintf(stderr, "invalid batch size or allocation failed\n");
            free(temp);
            zp_close(&c);
            return 1;
        }
        sprintf(temp, "%s.XXXXXX", dest);
        fd = mkstemp(temp);
        f = fd < 0 ? NULL : fdopen(fd, "w");
        if (!f) {
            fprintf(stderr, "cannot write the snapshot\n");
            if (fd >= 0) {
                close(fd);
                unlink(temp);
            }
            free(temp);
            zp_close(&c);
            return 1;
        }
        fprintf(f, "# id type value\n");
        for (base = 0; base < ZP_PARAM_COUNT; base += batch) {
            uint32_t k, nb = batch;
            size_t rlen2 = 0, n = 0;
            if (base + nb > ZP_PARAM_COUNT)
                nb = ZP_PARAM_COUNT - base;
            for (k = 0; k < nb; k++)
                ids[n++] = (uint16_t)(base + k);
            if (zp_id_query(&c, ids, n, reply, sizeof(reply), &rlen2) != ZP_OK) {
                fprintf(stderr, "query failed at 0x%04x\n", base);
                rc = 1;
                break;
            }
            if (rlen2 != n * ZP_ID_RECORD_BYTES) {
                fprintf(stderr, "incomplete reply at 0x%04x\n", base);
                rc = 1;
                break;
            }
            for (k = 0; k + ZP_ID_VALUE_BYTES <= rlen2; k += ZP_ID_VALUE_BYTES) {
                /* Reply records are big endian (id, value), like the query. */
                uint16_t echo = (uint16_t)((reply[k] << 8) | reply[k + 1]);
                uint16_t val = (uint16_t)(((uint32_t)reply[k + 2] << 8) |
                                          reply[k + 3]);
                if (echo != base + k / 4) {
                    fprintf(stderr, "unexpected reply id 0x%04x\n", echo);
                    rc = 1;
                    break;
                }
                fprintf(f, "%04x %04x %04x\n", echo, val, val);
            }
            if (rc)
                break;
            printf("\rsnapshot 0x%04x..0x%04x", base, base + nb - 1);
            fflush(stdout);
        }
        if (ferror(f))
            rc = 1;
        if (fclose(f) != 0)
            rc = 1;
        if (!rc && rename(temp, dest) != 0)
            rc = 1;
        if (rc) {
            unlink(temp);
            fprintf(stderr, "snapshot failed; destination unchanged\n");
        } else {
            printf("\nsnapshot written: %u ids\n", ZP_PARAM_COUNT);
        }
        free(temp);
        zp_close(&c);
        return rc;
    }
    if (!strcmp(cmd, "get")) {
        size_t rlen2 = 0;
        int r;
        size_t i;
        if (nids == 0) {
            fprintf(stderr, "need at least one --id\n");
            zp_close(&c);
            return 1;
        }
        r = zp_id_query(&c, ids, nids, reply, sizeof(reply), &rlen2);
        if (r != ZP_OK) {
            printf("id query failed: %s\n", zp_strerror((uint32_t)r));
            zp_close(&c);
            return 1;
        }
        printf("asked %zu id(s), got %zu byte(s)\n", nids, rlen2);
        printf("  %-9s %-9s %-11s %-8s  %s\n", "req_id", "echo_id", "value",
               "signed", "raw");
        for (i = 0; i + ZP_ID_VALUE_BYTES <= rlen2; i += ZP_ID_VALUE_BYTES) {
            uint16_t echo =
                (uint16_t)(((uint32_t)reply[i] << 8) | reply[i + 1]);
            uint16_t val =
                (uint16_t)(((uint32_t)reply[i + 2] << 8) | reply[i + 3]);
            uint16_t id = (i / ZP_ID_VALUE_BYTES < nids)
                              ? ids[i / ZP_ID_VALUE_BYTES]
                              : 0;
            int ok = (echo == id);
            int b;
            printf("  0x%04x     0x%04x     0x%04x     %6d   ", id, echo, val,
                   (int16_t)val);
            for (b = 0; b < ZP_ID_VALUE_BYTES; b++)
                printf("%02x", reply[i + b]);
            printf("%s\n", ok ? "" : "   <- ECHO MISMATCH");
        }
        if (rlen2 < nids * ZP_ID_VALUE_BYTES)
            printf("  (expected %zu byte(s) for %zu id(s), got %zu)\n",
                   nids * ZP_ID_VALUE_BYTES, nids, rlen2);
        zp_close(&c);
        return 0;
    }
    if (!strcmp(cmd, "idscan")) {
        uint32_t base, batch = id_batch ? id_batch : 16;
        uint32_t hits = 0, misses = 0, shortrep = 0;
        int show = verbose;
        static uint16_t hist[256];
        static uint32_t histn[256];
        int hn = 0;
        if (id_count == 0)
            id_count = 256;
        printf("scanning 0x%04x..0x%04x in batches of %u\n", id_from,
               id_from + id_count - 1, batch);
        for (base = id_from; base < id_from + id_count; base += batch) {
            uint32_t k, nb = batch;
            size_t rlen2 = 0, n = 0;
            if (base + nb > id_from + id_count)
                nb = id_from + id_count - base;
            for (k = 0; k < nb; k++)
                ids[n++] = (uint16_t)(base + k);
            if (zp_id_query(&c, ids, n, reply, sizeof(reply), &rlen2) != ZP_OK) {
                fprintf(stderr, "base 0x%04x: query failed\n", base);
                zp_close(&c);
                return 1;
            }
            if (!show)
                printf("\r  0x%04x..0x%04x ok (%zu B)", base,
                       base + nb - 1, rlen2);
            if (rlen2 != n * ZP_ID_VALUE_BYTES)
                shortrep++;
            for (k = 0; k + ZP_ID_VALUE_BYTES <= rlen2; k += ZP_ID_VALUE_BYTES) {
                uint16_t echo =
                    (uint16_t)(((uint32_t)reply[k] << 8) | reply[k + 1]);
                uint16_t val =
                    (uint16_t)(((uint32_t)reply[k + 2] << 8) | reply[k + 3]);
                int h;
                if (echo != (uint16_t)(base + k / 4))
                    printf("\n    id 0x%04x ECHO MISMATCH 0x%04x\n",
                           base + k / 4, echo);
                if (val == 0) {
                    misses++;
                    continue;
                }
                hits++;
                if (show)
                    printf("    id 0x%04x value 0x%04x (%d)\n", base + k / 4,
                           val, (int16_t)val);
                for (h = 0; h < hn; h++)
                    if (hist[h] == val)
                        break;
                if (h == hn && hn < 256) {
                    hist[hn] = val;
                    histn[hn] = 0;
                    hn++;
                }
                if (h < 256)
                    histn[h]++;
            }
        }
        if (!show)
            printf("\r");
        printf("\n%u id(s) non-zero, %u zero", hits, misses);
        if (shortrep)
            printf(", %u batch(es) returned a short reply", shortrep);
        printf("\n");
        printf("distinct values:\n");
        for (base = 0; base < (uint32_t)hn; base++)
            printf("  0x%04x (%6d)  x%u\n", hist[base],
                   (int16_t)hist[base], histn[base]);
        zp_close(&c);
        return 0;
    }
    if (!strcmp(cmd, "ping") || !strcmp(cmd, "sweep")) {
        int r;
        if (!strcmp(cmd, "sweep")) {
            size_t n;
            for (n = 0; n <= 8; n++) {
                uint8_t tmp[16];
                memset(tmp, 0, sizeof(tmp));
                r = zp_xfer(&c, tmp, n, ZP_CMD_ID, &rspcmd, reply,
                            sizeof(reply), &rlen);
                printf("len=%zu cmd=0x%02x -> %s", n, ZP_CMD_ID,
                       r == ZP_OK ? "REPLY" : zp_strerror((uint32_t)r));
                if (r == ZP_OK)
                    printf(" (%zu bytes, rsp cmd=0x%02x)", rlen, rspcmd);
                printf("\n");
                if (r == ZP_OK)
                    hexdump(stdout, reply, rlen);
            }
            zp_close(&c);
            return 0;
        }
        {
            uint8_t hs[2] = {0x00, 0x00};
            payload[0] = hs[0];
            payload[1] = hs[1];
            plen = sizeof(hs);
        }
        r = zp_xfer(&c, payload, plen, (uint8_t)cmdid, &rspcmd, reply,
                    sizeof(reply), &rlen);
        if (r != ZP_OK) {
            printf("no reply: %s\n", zp_strerror((uint32_t)r));
            rc = 1;
        } else {
            printf("reply cmd=0x%02x, %zu payload byte(s)\n", rspcmd, rlen);
            hexdump(stdout, reply, rlen);
        }
    } else if (!strcmp(cmd, "send")) {
        int r = zp_send(&c, payload, plen, (uint8_t)cmdid);
        printf("sent cmd=0x%02x, %zu payload byte(s), status %s\n", cmdid,
               plen,
               zp_strerror((uint32_t)r));
        rc = r != ZP_OK;
    } else if (!strcmp(cmd, "xfer")) {
        int r = zp_xfer(&c, payload, plen, (uint8_t)cmdid, &rspcmd, reply,
                        sizeof(reply), &rlen);
        if (r != ZP_OK) {
            printf("no reply: %s\n", zp_strerror((uint32_t)r));
            rc = 1;
        } else {
            printf("reply cmd=0x%02x, %zu payload byte(s)\n", rspcmd, rlen);
            hexdump(stdout, reply, rlen);
            if (out_path) {
                FILE *f = fopen(out_path, "wb");
                if (f) {
                    fwrite(reply, 1, rlen, f);
                    fclose(f);
                }
            }
        }
    } else if (!strcmp(cmd, "block")) {
        int r = zp_block_read(&c, (uint16_t)addr, reply);
        if (r != ZP_OK) {
            printf("block read 0x%04x failed: %s\n", addr,
                   zp_strerror((uint32_t)r));
            rc = 1;
        } else {
            printf("block 0x%04x (%d bytes)\n", addr, ZP_BLOCK_SIZE);
            hexdump(stdout, reply, ZP_BLOCK_SIZE);
            if (out_path) {
                FILE *f = fopen(out_path, "wb");
                if (f) {
                    fwrite(reply, 1, ZP_BLOCK_SIZE, f);
                    fclose(f);
                }
            }
        }
    } else if (!strcmp(cmd, "dump")) {
        uint8_t page[ZP_BLOCK_SIZE];
        FILE *f = out_path ? fopen(out_path, "wb") : NULL;
        uint32_t a;
        size_t total = 0;
        int r = ZP_OK;
        for (a = 0; a < (uint32_t)ZP_PARAM_BYTES; a += ZP_BLOCK_SIZE) {
            r = zp_block_read(&c, (uint16_t)a, page);
            if (r != ZP_OK) {
                fprintf(stderr, "block 0x%04x: %s\n", a,
                        zp_strerror((uint32_t)r));
                rc = 1;
                break;
            }
            total += ZP_BLOCK_SIZE;
            printf("\rread %zu bytes", total);
            fflush(stdout);
            if (f)
                fwrite(page, 1, ZP_BLOCK_SIZE, f);
        }
        if (f)
            fclose(f);
        if (rc == 0)
            printf("\nwrote %zu bytes to %s\n", total,
                   out_path ? out_path : "(no file)");
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
