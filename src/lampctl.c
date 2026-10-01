// lampctl.c - generic HID LampArray ("Windows Dynamic Lighting") controller for Linux
// NOTE: vibe-coded with Claude (Anthropic). Not audited; not yet verified on real hardware.
// See README.md for status and disclaimer. License: MIT.
// Build: gcc -O2 -Wall -o lampctl lampctl.c
//
// Nothing is hardcoded per model: the HID report descriptor of every /dev/hidraw*
// node is parsed at runtime to find LampArray reports, their IDs and field layout.
//
//   lampctl probe                    host info + every LampArray device found
//   lampctl lamps                    list lamps and their positions
//   lampctl set RRGGBB               all lamps, all devices
//   lampctl zone N RRGGBB            single lamp
//   lampctl range A B RRGGBB         lamps A..B
//   lampctl off                      all lamps black
//   lampctl auto                     give control back to the firmware
//   lampctl decode <hex descriptor>  offline: show the parsed layout
//
// Option: -d /dev/hidrawN  restricts the command to one node.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#define PAGE_LAMP 0x59
#define MAXREP 24
#define MAXFLD 96

enum { T_IN, T_OUT, T_FEAT };
// LampArray collection usages that identify a report's purpose
enum { K_ATTR = 0x02, K_REQ = 0x20, K_RESP = 0x22, K_MULTI = 0x50, K_RANGE = 0x60, K_CTRL = 0x70 };

typedef struct { uint16_t usage, off; uint8_t bits, idx; uint32_t lmax; } field_t;
typedef struct { uint8_t id, type; uint16_t kind; int nbits, nf; field_t f[MAXFLD]; } report_t;
typedef struct {
    int fd; char path[32], name[128]; uint16_t vid, pid;
    int nrep; report_t rep[MAXREP];
} lamp_dev;

/* ---------- descriptor parsing ---------- */

static void record(lamp_dev *d, int id, int type, int kind, int usage, int off, int bits, uint32_t lmax) {
    report_t *r = NULL;
    for (int i = 0; i < d->nrep; i++)
        if (d->rep[i].id == id && d->rep[i].type == type) r = &d->rep[i];
    if (!r) {
        if (d->nrep >= MAXREP) return;
        r = &d->rep[d->nrep++];
        r->id = id; r->type = type; r->kind = kind;
    }
    if (r->nf >= MAXFLD) return;
    field_t *f = &r->f[r->nf++];
    f->usage = usage; f->off = off; f->bits = bits; f->lmax = lmax;
    for (int j = 0; j < r->nf - 1; j++)
        if (r->f[j].usage == usage) f->idx++;
    if (off + bits > r->nbits) r->nbits = off + bits;
}

static void parse_desc(lamp_dev *d, const uint8_t *p, int n) {
    uint16_t page = 0, usages[256];
    uint32_t lmax = 0, umin = 0;
    int rsize = 0, rcount = 0, rid = 0, nu = 0, ncs = 0, nofs = 0;
    struct { uint16_t page, usage; } cs[16];
    struct { int id, type, off; } ot[64];
    int dummy = 0;

    for (int i = 0; i < n;) {
        uint8_t b = p[i++];
        if (b == 0xFE) { if (i < n) i += 2 + p[i]; continue; }   // long item: skip
        int sz = b & 3; if (sz == 3) sz = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        if (i + sz > n) break;
        uint32_t v = 0;
        for (int k = 0; k < sz; k++) v |= (uint32_t)p[i + k] << (8 * k);
        i += sz;

        if (type == 1) {                                   // global
            switch (tag) {
            case 0: page = v; break;
            case 2: lmax = v; break;
            case 7: rsize = v; break;
            case 8: rid = v; break;
            case 9: rcount = v; break;
            }
        } else if (type == 2) {                            // local
            if (tag == 0 && nu < 256) usages[nu++] = v;
            else if (tag == 1) umin = v;
            else if (tag == 2) for (uint32_t u = umin; u <= v && nu < 256; u++) usages[nu++] = u;
        } else if (type == 0) {                            // main
            if (tag == 0xA) {
                if (ncs < 16) { cs[ncs].page = page; cs[ncs].usage = nu ? usages[0] : 0; ncs++; }
            } else if (tag == 0xC) {
                if (ncs > 0) ncs--;
            } else if (tag == 8 || tag == 9 || tag == 0xB) {
                int t = tag == 8 ? T_IN : tag == 9 ? T_OUT : T_FEAT;
                int *off = NULL;
                for (int j = 0; j < nofs; j++)
                    if (ot[j].id == rid && ot[j].type == t) off = &ot[j].off;
                if (!off && nofs < 64) {
                    ot[nofs].id = rid; ot[nofs].type = t; ot[nofs].off = 0;
                    off = &ot[nofs++].off;
                }
                if (!off) off = &dummy;
                int kind = 0;
                if (page == PAGE_LAMP && t != T_IN)
                    for (int c = ncs - 1; c >= 0; c--)
                        if (cs[c].page == PAGE_LAMP && cs[c].usage != 0x01) { kind = cs[c].usage; break; }
                for (int k = 0; k < rcount; k++) {
                    uint16_t u = nu ? usages[k < nu ? k : nu - 1] : 0;
                    if (kind && u && rid) record(d, rid, t, kind, u, *off, rsize, lmax);
                    *off += rsize;
                }
            }
            nu = 0;                                        // locals reset after each main item
        }
    }
}

/* ---------- report field access ---------- */

static report_t *rep(lamp_dev *d, int kind) {
    for (int i = 0; i < d->nrep; i++)
        if (d->rep[i].kind == kind) return &d->rep[i];
    return NULL;
}
static int rlen(report_t *r) { return 1 + (r->nbits + 7) / 8; }

static field_t *fld(report_t *r, int usage, int idx) {
    for (int i = 0; i < r->nf; i++)
        if (r->f[i].usage == usage && r->f[i].idx == idx) return &r->f[i];
    return NULL;
}
static void put(report_t *r, uint8_t *buf, int usage, int idx, uint32_t v) {
    field_t *f = fld(r, usage, idx);
    if (!f) return;
    for (int b = 0; b < f->bits && b < 32; b++) {
        int pos = f->off + b;
        uint8_t *p = buf + 1 + pos / 8, m = 1 << (pos % 8);
        if ((v >> b) & 1) *p |= m; else *p &= ~m;
    }
}
static uint32_t get(report_t *r, const uint8_t *buf, int usage, int idx) {
    field_t *f = fld(r, usage, idx);
    uint32_t v = 0;
    if (!f) return 0;
    for (int b = 0; b < f->bits && b < 32; b++) {
        int pos = f->off + b;
        if (buf[1 + pos / 8] & (1 << (pos % 8))) v |= 1u << b;
    }
    return v;
}
// colour channel: scale 0..255 to the field's logical maximum
static void put_level(report_t *r, uint8_t *buf, int usage, int idx, uint8_t v) {
    field_t *f = fld(r, usage, idx);
    if (f) put(r, buf, usage, idx, (uint32_t)((uint64_t)v * (f->lmax ? f->lmax : 255) / 255));
}

static int send_report(lamp_dev *d, report_t *r, uint8_t *buf) {
    int len = rlen(r);
    int rc = (r->type == T_FEAT) ? ioctl(d->fd, HIDIOCSFEATURE(len), buf)
                                 : (int)write(d->fd, buf, len);
    if (rc < 0) perror(r->type == T_FEAT ? "set feature" : "write");
    return rc < 0 ? -1 : 0;
}
static int fetch(lamp_dev *d, report_t *r, uint8_t *buf) {
    int len = rlen(r);
    memset(buf, 0, len);
    buf[0] = r->id;
    int rc = ioctl(d->fd, HIDIOCGFEATURE(len), buf);
    if (rc < 0) perror("get feature");
    return rc < 0 ? -1 : 0;
}
static void blank(report_t *r, uint8_t *buf) { memset(buf, 0, rlen(r)); buf[0] = r->id; }

/* ---------- LampArray operations ---------- */

// out: count, width, height, depth, kind, min interval
static int attrs(lamp_dev *d, uint32_t out[6]) {
    static const int u[6] = { 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    uint8_t buf[512];
    report_t *r = rep(d, K_ATTR);
    if (!r || r->type != T_FEAT || rlen(r) > (int)sizeof buf || fetch(d, r, buf) < 0) return -1;
    for (int i = 0; i < 6; i++) out[i] = get(r, buf, u[i], 0);
    return 0;
}

static int autonomous(lamp_dev *d, int on) {
    uint8_t buf[512];
    report_t *r = rep(d, K_CTRL);
    if (!r) return 0;                       // device has no autonomous mode
    blank(r, buf);
    put(r, buf, 0x71, 0, on ? 1 : 0);
    return send_report(d, r, buf);
}

static int paint(lamp_dev *d, int a, int z, uint8_t R, uint8_t G, uint8_t B) {
    uint8_t buf[512];
    report_t *r = rep(d, K_RANGE);
    if (r) {                                // preferred: one report for the whole range
        blank(r, buf);
        put(r, buf, 0x55, 0, 1);            // LampUpdateComplete
        put(r, buf, 0x61, 0, a);
        put(r, buf, 0x62, 0, z);
        put_level(r, buf, 0x51, 0, R); put_level(r, buf, 0x52, 0, G);
        put_level(r, buf, 0x53, 0, B); put_level(r, buf, 0x54, 0, 255);
        return send_report(d, r, buf);
    }
    r = rep(d, K_MULTI);                    // fallback: batches of N lamps
    if (!r) return -1;
    int cap = 0;
    for (int i = 0; i < r->nf; i++) if (r->f[i].usage == 0x21) cap++;
    if (cap == 0) return -1;
    for (int s = a; s <= z; s += cap) {
        int n = z - s + 1 < cap ? z - s + 1 : cap;
        blank(r, buf);
        put(r, buf, 0x03, 0, n);
        put(r, buf, 0x55, 0, s + n > z ? 1 : 0);
        for (int i = 0; i < n; i++) {
            put(r, buf, 0x21, i, s + i);
            put_level(r, buf, 0x51, i, R); put_level(r, buf, 0x52, i, G);
            put_level(r, buf, 0x53, i, B); put_level(r, buf, 0x54, i, 255);
        }
        if (send_report(d, r, buf) < 0) return -1;
    }
    return 0;
}

/* ---------- discovery ---------- */

static lamp_dev *open_dev(const char *path, int *eacces) {
    int fd = open(path, O_RDWR);
    if (fd < 0) { if (errno == EACCES && eacces) (*eacces)++; return NULL; }
    struct hidraw_devinfo di;
    struct hidraw_report_descriptor rd;
    int size = 0;
    if (ioctl(fd, HIDIOCGRAWINFO, &di) < 0 || ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 ||
        size <= 0 || size > (int)sizeof rd.value) { close(fd); return NULL; }
    rd.size = size;
    if (ioctl(fd, HIDIOCGRDESC, &rd) < 0) { close(fd); return NULL; }

    lamp_dev *d = calloc(1, sizeof *d);
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    snprintf(d->path, sizeof d->path, "%s", path);
    d->vid = (uint16_t)di.vendor; d->pid = (uint16_t)di.product;
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof d->name), d->name) < 0) strcpy(d->name, "?");
    parse_desc(d, rd.value, size);
    if (!rep(d, K_RANGE) && !rep(d, K_MULTI)) { close(fd); free(d); return NULL; }
    return d;
}

static int discover(const char *only, lamp_dev **out, int max) {
    int n = 0, eacces = 0;
    if (only) {
        lamp_dev *d = open_dev(only, &eacces);
        if (d) out[n++] = d;
    } else {
        char path[32];
        for (int i = 0; i < 64 && n < max; i++) {
            snprintf(path, sizeof path, "/dev/hidraw%d", i);
            lamp_dev *d = open_dev(path, &eacces);
            if (d) out[n++] = d;
        }
    }
    if (!n) {
        fprintf(stderr, "no LampArray device found.\n");
        if (eacces) fprintf(stderr, "%d hidraw node(s) were not readable: try sudo or add a udev rule.\n", eacces);
    }
    return n;
}

/* ---------- commands ---------- */

static const char *kname(int k) {
    switch (k) {
    case K_ATTR: return "Attributes"; case K_REQ: return "AttrRequest"; case K_RESP: return "AttrResponse";
    case K_MULTI: return "MultiUpdate"; case K_RANGE: return "RangeUpdate"; case K_CTRL: return "Control";
    }
    return "?";
}
static const char *lamp_kind(unsigned k) {
    static const char *n[] = { "Undefined", "Keyboard", "Mouse", "GameController", "Peripheral",
                               "Scene", "Notification", "Chassis", "Wearable", "Furniture", "Art" };
    return k < 11 ? n[k] : "?";
}

static void print_layout(lamp_dev *d) {
    static const char *tn[] = { "input", "output", "feature" };
    for (int i = 0; i < d->nrep; i++) {
        report_t *r = &d->rep[i];
        printf("    report 0x%02x %-7s %-13s %3d bytes, %2d fields\n",
               r->id, tn[r->type], kname(r->kind), rlen(r), r->nf);
    }
}

static void dmi(const char *name, char *out, size_t n) {
    char p[96];
    snprintf(p, sizeof p, "/sys/class/dmi/id/%s", name);
    FILE *f = fopen(p, "r");
    snprintf(out, n, "?");
    if (f) { if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0; fclose(f); }
}

static void cmd_probe(lamp_dev **ds, int n) {
    char a[96], b[96], c[96];
    dmi("sys_vendor", a, sizeof a); dmi("product_name", b, sizeof b); dmi("product_version", c, sizeof c);
    printf("host: %s %s (%s)\n", a, b, c);
    for (int i = 0; i < n; i++) {
        lamp_dev *d = ds[i];
        uint32_t at[6];
        printf("\n%s  %04x:%04x  %s\n", d->path, d->vid, d->pid, d->name);
        print_layout(d);
        if (attrs(d, at) == 0)
            printf("    lamps=%u kind=%s box=%.1fx%.1fx%.1f mm min-interval=%u us\n", at[0], lamp_kind(at[4]),
                   at[1] / 1000.0, at[2] / 1000.0, at[3] / 1000.0, at[5]);
    }
}

static void cmd_lamps(lamp_dev *d) {
    uint32_t at[6];
    uint8_t buf[512];
    report_t *rq = rep(d, K_REQ), *rs = rep(d, K_RESP);
    if (!rq || !rs || attrs(d, at) < 0) { printf("%s: per-lamp attributes not available\n", d->path); return; }
    printf("%s: %u lamps\n", d->path, at[0]);
    for (uint32_t i = 0; i < at[0]; i++) {
        blank(rq, buf);
        put(rq, buf, 0x21, 0, i);
        if (send_report(d, rq, buf) < 0 || fetch(d, rs, buf) < 0) return;
        printf("  lamp %3u: x=%.1f y=%.1f z=%.1f mm purposes=0x%x\n", get(rs, buf, 0x21, 0),
               get(rs, buf, 0x23, 0) / 1000.0, get(rs, buf, 0x24, 0) / 1000.0,
               get(rs, buf, 0x25, 0) / 1000.0, get(rs, buf, 0x26, 0));
    }
}

static int parse_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b) {
    unsigned v;
    if (*s == '#') s++;
    if (strlen(s) != 6 || sscanf(s, "%x", &v) != 1) return -1;
    *r = v >> 16; *g = (v >> 8) & 0xff; *b = v & 0xff;
    return 0;
}

static int decode_hex(const char *hex) {
    uint8_t buf[4096];
    int n = 0, hi = -1;
    for (; *hex; hex++) {
        if (!isxdigit((unsigned char)*hex)) continue;
        int v = isdigit((unsigned char)*hex) ? *hex - '0' : (tolower(*hex) - 'a' + 10);
        if (hi < 0) hi = v;
        else { if (n < (int)sizeof buf) buf[n++] = hi * 16 + v; hi = -1; }
    }
    lamp_dev *d = calloc(1, sizeof *d);
    if (!d) return 1;
    parse_desc(d, buf, n);
    printf("%d descriptor bytes, %d LampArray report(s)\n", n, d->nrep);
    print_layout(d);
    free(d);
    return 0;
}

static void usage(void) {
    fprintf(stderr, "usage: lampctl [-d /dev/hidrawN] probe|lamps|set RRGGBB|zone N RRGGBB|"
                    "range A B RRGGBB|off|auto\n       lampctl decode <hex descriptor>\n");
}

int main(int argc, char **argv) {
    const char *only = NULL;
    int ai = 1;
    if (ai + 1 < argc && !strcmp(argv[ai], "-d")) { only = argv[ai + 1]; ai += 2; }
    if (ai >= argc) { usage(); return 1; }
    const char *cmd = argv[ai++];
    int nargs = argc - ai;

    if (!strcmp(cmd, "decode") && nargs == 1) return decode_hex(argv[ai]);

    uint8_t R = 0, G = 0, B = 0;
    int a = 0, z = -1, mode = 0;       // mode: 1 all, 2 range, 3 auto
    if (!strcmp(cmd, "probe") || !strcmp(cmd, "lamps")) {
    } else if (!strcmp(cmd, "set") && nargs == 1 && !parse_color(argv[ai], &R, &G, &B)) mode = 1;
    else if (!strcmp(cmd, "off") && nargs == 0) mode = 1;
    else if (!strcmp(cmd, "zone") && nargs == 2 && !parse_color(argv[ai + 1], &R, &G, &B)) { a = z = atoi(argv[ai]); mode = 2; }
    else if (!strcmp(cmd, "range") && nargs == 3 && !parse_color(argv[ai + 2], &R, &G, &B)) { a = atoi(argv[ai]); z = atoi(argv[ai + 1]); mode = 2; }
    else if (!strcmp(cmd, "auto") && nargs == 0) mode = 3;
    else { usage(); return 1; }

    lamp_dev *ds[16];
    int n = discover(only, ds, 16), rc = 0;
    if (!n) return 1;

    if (!strcmp(cmd, "probe")) cmd_probe(ds, n);
    else if (!strcmp(cmd, "lamps")) for (int i = 0; i < n; i++) cmd_lamps(ds[i]);
    else for (int i = 0; i < n; i++) {
        lamp_dev *d = ds[i];
        uint32_t at[6];
        if (mode == 3) { if (autonomous(d, 1) < 0) rc = 1; continue; }
        if (attrs(d, at) < 0 || at[0] == 0) { fprintf(stderr, "%s: cannot read lamp count\n", d->path); rc = 1; continue; }
        int lo = a, hi = mode == 1 ? (int)at[0] - 1 : z;
        if (hi >= (int)at[0] || lo < 0 || lo > hi) { fprintf(stderr, "%s: lamp range out of bounds (0..%u)\n", d->path, at[0] - 1); rc = 1; continue; }
        if (autonomous(d, 0) < 0 || paint(d, lo, hi, R, G, B) < 0) rc = 1;
    }
    for (int i = 0; i < n; i++) { close(ds[i]->fd); free(ds[i]); }
    return rc;
}
