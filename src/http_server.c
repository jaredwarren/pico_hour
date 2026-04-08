/*
 * http_server.c — tiny HTTP server on top of lwIP raw TCP API
 *
 * Why not “real” sockets (POSIX)?
 *  On this Pico SDK build, lwIP runs with NO_SYS=1: no RTOS, no blocking socket thread.
 *  TCP is callback-driven: lwIP calls http_accept when a client connects, http_recv when data arrives.
 *  cyw43_arch_poll() (in main) drives the stack forward.
 *
 * Why critical_section for RGB?
 *  http_recv runs in the context of lwIP during cyw43_arch_poll(); the main loop reads color when
 *  drawing. Without a lock, you could read half-updated bytes (torn read). critical_section_* disables
 *  interrupts briefly on the one core — enough for three uint8_t (not a full mutex, but correct here).
 *
 * cyw43_arch_lwip_begin/end around tcp_bind/listen:
 *  SDK wrapper to keep lwIP internal state consistent when calling from “outside” lwIP callbacks.
 */

#include "http_server.h"

#include "config.h"
#include "pico/cyw43_arch.h"
#include "pico/sync.h"

#include "lwip/ip.h"    /* IP4_ADDR, ip_addr_t */
#include "lwip/pbuf.h"  /* pbuf chain — TCP payload may be split across buffers */
#include "lwip/tcp.h"   /* tcp_pcb, tcp_listen, callbacks */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static critical_section_t color_lock;
static uint8_t color_r = 32;
static uint8_t color_g = 0;
static uint8_t color_b = 128;

/* Listening PCB stays allocated for the lifetime of the firmware (we never close the listener). */
static struct tcp_pcb *listen_pcb;

void http_get_color(uint8_t *r, uint8_t *g, uint8_t *b) {
    critical_section_enter_blocking(&color_lock);
    *r = color_r;
    *g = color_g;
    *b = color_b;
    critical_section_exit(&color_lock);
}

static void set_color(uint8_t r, uint8_t g, uint8_t b) {
    critical_section_enter_blocking(&color_lock);
    color_r = r;
    color_g = g;
    color_b = b;
    critical_section_exit(&color_lock);
}

/* HTTP request line ends at \r or \n; we only need the first line to route GET /color vs /status. */
static void copy_first_line(const char *buf, size_t len, char *out, size_t outsz) {
    size_t i = 0;
    while (i < len && i + 1 < outsz) {
        char c = buf[i];
        if (c == '\r' || c == '\n') {
            break;
        }
        out[i] = c;
        i++;
    }
    out[i] = '\0'; /* C strings end with NUL — strlen/printf stop here */
}

/*
 * Very small query parser: find ?r=12&g=… in the request line.
 * strstr searches for substrings; strtoul converts ASCII digits to unsigned long (base 10).
 * We clamp to 0–255 for LED PWM-style bytes.
 */
static void parse_rgb_query(const char *line, uint8_t *r, uint8_t *g, uint8_t *b) {
    const char *q = strchr(line, '?');
    if (!q) {
        return;
    }
    /* Anonymous struct array: each entry maps a literal key to the uint8_t we should update. */
    const struct {
        const char *key;
        uint8_t *dst;
    } keys[] = {
        {"r=", r},
        {"g=", g},
        {"b=", b},
    };
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        const char *p = strstr(q, keys[k].key);
        if (!p) {
            continue;
        }
        p += strlen(keys[k].key);
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        /* end != p ensures we actually parsed digits; strtoul allows leading whitespace */
        if (end != p && v <= 255UL) {
            *keys[k].dst = (uint8_t)v;
        }
    }
}

/*
 * Queue a full HTTP response and close. TCP_WRITE_FLAG_COPY copies data into lwIP’s memory
 * (our stack buffer is safe to reuse after return).
 */
static err_t send_and_close(struct tcp_pcb *tpcb, const char *data, size_t len) {
    err_t e = tcp_write(tpcb, data, len, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        tcp_abort(tpcb);
        return e;
    }
    tcp_output(tpcb); /* push buffers to wire promptly */
    tcp_recv(tpcb, NULL);
    err_t ce = tcp_close(tpcb);
    return ce;
}

/*
 * lwIP calls this when data arrives on a connection we accepted.
 * p == NULL means the remote closed; we just return (our pcb may already be closing).
 */
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg; /* unused parameter — silences compiler warnings in strict builds */
    if (err != ERR_OK) {
        if (p != NULL) {
            pbuf_free(p);
        }
        tcp_abort(tpcb);
        return err;
    }
    if (p == NULL) {
        return ERR_OK;
    }

    /*
     * pbuf can be a linked list (chained fragments). We flatten the first ~320 bytes into raw[]
     * so we can NUL-terminate and use string helpers. Real servers handle arbitrary length; this
     * is enough for GET /color?r=… HTTP/1.1\r\n.
     */
    char raw[320];
    size_t total = 0;
    for (struct pbuf *pb = p; pb != NULL; pb = pb->next) {
        size_t chunk = pb->len;
        if (total + chunk >= sizeof(raw)) {
            chunk = sizeof(raw) - 1U - total;
        }
        if (chunk == 0U) {
            break;
        }
        memcpy(raw + total, pb->payload, chunk);
        total += chunk;
    }
    raw[total] = '\0';
    pbuf_free(p); /* lwIP refcount — must free what recv gave us */

    char line[256];
    copy_first_line(raw, total, line, sizeof(line));

    /* Prefix match: first 10 chars must be "GET /color" (so /color?… works; odd paths are still accepted). */
    if (strncmp(line, "GET /color", 10) == 0) {
        uint8_t r = 0, g = 0, b = 0;
        http_get_color(&r, &g, &b);   /* start from current color */
        parse_rgb_query(line, &r, &g, &b); /* override only params present in URL */
        set_color(r, g, b);
        static const char resp[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK";
        /* sizeof(resp)-1: don’t send the string’s trailing NUL byte as body content */
        return send_and_close(tpcb, resp, sizeof(resp) - 1U);
    }

    if (strncmp(line, "GET /status", 11) == 0) {
        uint8_t r = 0, g = 0, b = 0;
        http_get_color(&r, &g, &b);
        char body[64];
        int bl = snprintf(body, sizeof(body), "{\"r\":%u,\"g\":%u,\"b\":%u}\r\n", (unsigned)r, (unsigned)g, (unsigned)b);
        if (bl < 0 || (size_t)bl >= sizeof(body)) {
            bl = (int)strlen("{\"r\":0,\"g\":0,\"b\":0}\r\n");
            memcpy(body, "{\"r\":0,\"g\":0,\"b\":0}\r\n", (size_t)bl + 1U);
        }
        char hdr[128];
        int hl = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.0 200 OK\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %d\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          bl);
        if (hl < 0 || (size_t)hl >= sizeof(hdr)) {
            tcp_abort(tpcb);
            return ERR_MEM;
        }
        if (tcp_write(tpcb, hdr, (u16_t)hl, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            tcp_abort(tpcb);
            return ERR_MEM;
        }
        if (tcp_write(tpcb, body, (u16_t)bl, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            tcp_abort(tpcb);
            return ERR_MEM;
        }
        tcp_output(tpcb);
        tcp_recv(tpcb, NULL);
        return tcp_close(tpcb);
    }

    static const char nf[] =
        "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not found";
    return send_and_close(tpcb, nf, sizeof(nf) - 1U);
}

/* Optional error callback — lwIP notifies us if the connection RSTs, etc. */
static void http_err(void *arg, err_t err) {
    (void)arg;
    (void)err;
}

/*
 * New inbound connection: register recv/err handlers on the per-client pcb.
 * listen_pcb keeps accepting; each client gets its own tcp_pcb.
 */
static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }
    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, http_recv);
    tcp_err(newpcb, http_err);
    return ERR_OK;
}

void http_server_init(void) {
    critical_section_init(&color_lock);

    cyw43_arch_lwip_begin();
    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        cyw43_arch_lwip_end();
        return;
    }
    /* 0.0.0.0 = listen on all IPv4 addresses assigned to the interface */
    ip_addr_t bind_addr;
    IP4_ADDR(&bind_addr, 0, 0, 0, 0);
    err_t e = tcp_bind(pcb, &bind_addr, HTTP_SERVER_PORT);
    if (e != ERR_OK) {
        tcp_abort(pcb);
        cyw43_arch_lwip_end();
        return;
    }
    /* tcp_listen consumes pcb and returns a listen PCB (or NULL on failure). */
    listen_pcb = tcp_listen(pcb);
    if (listen_pcb == NULL) {
        tcp_abort(pcb);
        cyw43_arch_lwip_end();
        return;
    }
    tcp_accept(listen_pcb, http_accept);
    cyw43_arch_lwip_end();
}

void http_server_poll(void) {
    /* lwIP timers and TCP via cyw43_arch_poll() in main */
}
