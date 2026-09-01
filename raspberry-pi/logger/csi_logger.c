/* csi-logger - read CSI CSV from the ESP32 RX, stamp each line on arrival.
 *
 * Job: read serial, prepend a CLOCK_MONOTONIC timestamp, append to a file.
 * Nothing else. No parsing of the CSI payload, no maths - that all happens on
 * the PC.
 *
 * Output line is the ESP32 line with pi_timestamp_ms prepended:
 *
 *   pi_timestamp_ms,node_id,seq,t_us,rssi,noise_floor,sig_mode,len,
 *   first_word_invalid,dropped,<int8 values>
 *
 * Why C and not Python: keeps python3 + pyserial off the image, and gives a
 * predictable clock_gettime() immediately after read() returns.
 *
 * See docs/RASPBERRY_PI_V1.md, "Serial logger".
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define READ_BUF     4096
/* One CSI line is ~870 B today. 8 KB leaves room for the 384-value case and
 * still bounds a runaway line that never sees a newline. */
#define LINE_BUF     8192
#define FLUSH_EVERY_MS 1000
#define REOPEN_DELAY_US 500000

static volatile sig_atomic_t running = 1;

static void on_sigint(int sig)
{
    (void)sig;
    running = 0;
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------ */
/* Line assembly                                                       */
/* ------------------------------------------------------------------ */

/* Carries a partial line between read() bursts. USB hands us arbitrary chunk
 * boundaries, so a read can end mid-line, mid-number, or exactly on the '\n'. */
typedef struct {
    char   buf[LINE_BUF];
    size_t len;
    int    overflow;   /* current line got too long; drop it at the newline */
} line_asm_t;

/* Called for each complete line. Returns nothing - output errors are handled
 * by the caller's stream state. */
typedef void (*line_cb)(void *ctx, const char *line, size_t len, int64_t t_ms);

/* Feed one read() burst. Every complete line in the burst gets the same
 * timestamp - one clock read per burst, not per byte. */
static void line_asm_feed(line_asm_t *a, const char *data, size_t n,
                          int64_t t_ms, line_cb cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        char c = data[i];

        if (c == '\n') {
            if (!a->overflow) {
                size_t len = a->len;
                /* tolerate CRLF, though the ESP32 sends bare LF */
                if (len && a->buf[len - 1] == '\r') {
                    len--;
                }
                if (len) {
                    a->buf[len] = '\0';
                    cb(ctx, a->buf, len, t_ms);
                }
            }
            a->len = 0;
            a->overflow = 0;
            continue;
        }

        if (a->len + 1 >= sizeof(a->buf)) {
            /* No newline in a full buffer: this is not a CSI line. Drop it and
             * resynchronise at the next newline rather than emitting garbage. */
            a->overflow = 1;
            a->len = 0;
            continue;
        }
        a->buf[a->len++] = c;
    }
}

/* ------------------------------------------------------------------ */
/* Serial port                                                         */
/* ------------------------------------------------------------------ */

static int serial_open(const char *path)
{
    int fd = open(path, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    /* Raw, non-canonical. Canonical mode buffers inside the tty layer, which
     * would destroy the arrival timestamp we are trying to take. */
    cfmakeraw(&tio);
    cfsetispeed(&tio, B921600);
    cfsetospeed(&tio, B921600);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN] = 1;   /* return as soon as a byte is available */
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE       *fp;
    const char *node_id;
    size_t      node_id_len;
    uint64_t    kept;
    uint64_t    dropped_prefix;
} writer_t;

static void write_line(void *ctx, const char *line, size_t len, int64_t t_ms)
{
    writer_t *w = (writer_t *)ctx;

    /* Belt and braces against boot messages and line noise: anything not
     * starting with the node_id is not ours. Milestone 1 confirmed the ESP32
     * bootloader emits non-ASCII bytes at a different baud before the app
     * silences logging. */
    if (len < w->node_id_len ||
        memcmp(line, w->node_id, w->node_id_len) != 0 ||
        line[w->node_id_len] != ',') {
        w->dropped_prefix++;
        return;
    }

    fprintf(w->fp, "%lld,%s\n", (long long)t_ms, line);
    w->kept++;
}

/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s -p <port> -o <out.csv> [-n <node_id>]\n"
            "  -p  serial device      (default /dev/esp32-rx)\n"
            "  -o  output csv         (default csi.csv)\n"
            "  -n  node id to keep    (default RX1)\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *port = "/dev/esp32-rx";
    const char *out = "csi.csv";
    const char *node_id = "RX1";

    int opt;
    while ((opt = getopt(argc, argv, "p:o:n:h")) != -1) {
        switch (opt) {
        case 'p': port = optarg; break;
        case 'o': out = optarg; break;
        case 'n': node_id = optarg; break;
        default:  usage(argv[0]); return opt == 'h' ? 0 : 2;
        }
    }

    /* Default SIGINT would kill us before the final fflush. The stop script
     * sends SIGINT precisely so the file closes cleanly. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    writer_t w;
    memset(&w, 0, sizeof(w));
    w.node_id = node_id;
    w.node_id_len = strlen(node_id);
    w.fp = fopen(out, "a");
    if (!w.fp) {
        fprintf(stderr, "csi-logger: cannot open %s: %s\n", out, strerror(errno));
        return 1;
    }

    line_asm_t asmb;
    memset(&asmb, 0, sizeof(asmb));

    int fd = -1;
    char buf[READ_BUF];
    int64_t last_flush = monotonic_ms();

    while (running) {
        if (fd < 0) {
            fd = serial_open(port);
            if (fd < 0) {
                if (!running) break;
                usleep(REOPEN_DELAY_US);
                continue;
            }
            /* A reconnect means the ESP32 reset and re-enumerated. Whatever was
             * half-received belongs to the previous session - throw it away. */
            asmb.len = 0;
            asmb.overflow = 0;
            fprintf(stderr, "csi-logger: opened %s\n", port);
        }

        ssize_t n = read(fd, buf, sizeof(buf));
        int64_t t_ms = monotonic_ms();   /* immediately after read() returns */

        if (n > 0) {
            line_asm_feed(&asmb, buf, (size_t)n, t_ms, write_line, &w);
        } else if (n == 0 || (n < 0 && (errno == EIO || errno == ENXIO || errno == ENODEV))) {
            /* Device went away - ESP32 reset, USB re-enumerated. */
            fprintf(stderr, "csi-logger: %s went away, reopening\n", port);
            close(fd);
            fd = -1;
            usleep(REOPEN_DELAY_US);
            continue;
        } else if (n < 0 && errno != EINTR && errno != EAGAIN) {
            fprintf(stderr, "csi-logger: read: %s\n", strerror(errno));
            close(fd);
            fd = -1;
            usleep(REOPEN_DELAY_US);
            continue;
        }

        /* Flush on a timer, not per line. At ~44 KB/s a crash costs <= ~44 KB,
         * and per-line flushing would stall the read loop. */
        if (t_ms - last_flush >= FLUSH_EVERY_MS) {
            fflush(w.fp);
            last_flush = t_ms;
        }
    }

    if (fd >= 0) {
        close(fd);
    }
    fflush(w.fp);
    fclose(w.fp);

    fprintf(stderr, "csi-logger: %llu lines written, %llu non-%s lines dropped\n",
            (unsigned long long)w.kept, (unsigned long long)w.dropped_prefix, node_id);
    return 0;
}
