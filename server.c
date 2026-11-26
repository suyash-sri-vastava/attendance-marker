/*
 * ATTENDANCE PLANNER  -  FOCP assignment
 *
 * Problem: Colleges need 75% attendance to sit in exams. Students keep
 * guessing "how many classes can I still skip?" or "how many must I attend
 * to get back to safe?". This program answers that for every subject.
 *
 * Everything is plain C (standard library + Linux sockets). No frameworks.
 * The program is its own tiny web server, so one file = the whole project.
 *
 * Build:  gcc -O2 -Wall -Wextra -o attendance server.c
 * Run  :  ./attendance          then open http://localhost:8080
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ================= EDIT THIS PART ================= */
#define TEAM_NAME "Team Name Here"
static const char *MEMBERS[][2] = {
    /* { "Member name",   "Scholar number" }, */
    {"Member One",   "0000000001"},
    {"Member Two",   "0000000002"},
    {"Member Three", "0000000003"},
};
/* ================================================== */

#define N_MEMBERS   (sizeof MEMBERS / sizeof MEMBERS[0])
#define MAX_SUBJECTS 8
#define NAME_LEN     40
#define REQ_MAX      8192
#define PAGE_MAX     32768

/* ---------- Data for one subject ---------- */
typedef struct {
    char   name[NAME_LEN];
    int    held;        /* classes conducted so far     */
    int    attended;    /* classes the student attended */
    double percent;     /* attended / held * 100        */
    int    can_miss;    /* how many more classes can be skipped (if safe) */
    int    must_attend; /* classes to attend in a row (if short); -1 = impossible */
} Subject;

/* ---------- The actual problem-solving logic ---------- */
static void analyse(Subject *s, int target)
{
    s->percent     = s->held > 0 ? 100.0 * s->attended / s->held : 0.0;
    s->can_miss    = 0;
    s->must_attend = 0;

    if (s->attended * 100 >= target * s->held) {
        /* Safe. Largest x with  attended/(held+x) >= target/100
           =>  x = floor(100*attended/target) - held                */
        s->can_miss = (s->attended * 100) / target - s->held;
    } else if (target >= 100) {
        s->must_attend = -1;               /* can never climb back to 100% */
    } else {
        /* Short. Smallest y with (attended+y)/(held+y) >= target/100
           =>  y = ceil( (target*held - 100*attended) / (100-target) ) */
        int num = target * s->held - 100 * s->attended;
        int den = 100 - target;
        s->must_attend = (num + den - 1) / den;
    }
}

/* ---------- Tiny string-builder (so we never overflow a buffer) ---------- */
typedef struct { char data[PAGE_MAX]; size_t len; } Buf;

static void bprintf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (b->len < sizeof b->data - 1) {
        int n = vsnprintf(b->data + b->len, sizeof b->data - b->len, fmt, ap);
        if (n > 0) {
            b->len += (size_t)n;
            if (b->len > sizeof b->data - 1) b->len = sizeof b->data - 1;
        }
    }
    va_end(ap);
}

/* Write user text safely into HTML (stops <script> tricks). */
static void bescape(Buf *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
            case '&':  bprintf(b, "&amp;");  break;
            case '<':  bprintf(b, "&lt;");   break;
            case '>':  bprintf(b, "&gt;");   break;
            case '"':  bprintf(b, "&quot;"); break;
            case '\'': bprintf(b, "&#39;");  break;
            default:   bprintf(b, "%c", *s);
        }
    }
}

/* ---------- Reading form data sent by the browser ---------- */
static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') {
            *o++ = ' '; s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

static int form_get(const char *body, const char *key, char *out, size_t outsz)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t n = strcspn(p, "&");
            if (n >= outsz) n = outsz - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            url_decode(out);
            return 1;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
    return 0;
}

/* Turn text into a whole number; returns 0 if it isn't a valid number in [lo,hi]. */
static int parse_int(const char *text, int lo, int hi, int *result)
{
    char *end;
    long v = strtol(text, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (text == end || *end != '\0' || v < lo || v > hi) return 0;
    *result = (int)v;
    return 1;
}

/* ---------- HTML pieces ---------- */
static void page_start(Buf *b, const char *title)
{
    bprintf(b,
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>%s</title><style>"
        "*{box-sizing:border-box}"
        "body{font-family:system-ui,Arial,sans-serif;margin:0;background:#f3f5f9;color:#1c2333}"
        ".wrap{max-width:720px;margin:0 auto;padding:16px}"
        ".team{background:#1f3a8a;color:#fff;border-radius:14px;padding:18px;margin-bottom:16px}"
        ".team h1{margin:0 0 4px;font-size:1.5rem}"
        ".team p{margin:0 0 10px;opacity:.85}"
        ".team table{width:100%%;border-collapse:collapse;font-size:.95rem}"
        ".team th,.team td{text-align:left;padding:5px 0;border-top:1px solid rgba(255,255,255,.25)}"
        ".card{background:#fff;border-radius:14px;padding:16px;margin-bottom:16px;box-shadow:0 1px 4px rgba(0,0,0,.08)}"
        "h2{margin:0 0 8px;font-size:1.15rem}"
        ".row{display:grid;grid-template-columns:2fr 1fr 1fr;gap:8px;margin-bottom:8px}"
        ".row.head{font-size:.8rem;color:#5b6478;font-weight:600}"
        "input{width:100%%;padding:10px;border:1px solid #c5cbd8;border-radius:8px;font-size:1rem}"
        "button,.btn{display:inline-block;background:#1f3a8a;color:#fff;border:0;border-radius:10px;"
        "padding:12px 20px;font-size:1rem;text-decoration:none;cursor:pointer}"
        ".req{display:flex;align-items:center;gap:10px;margin-bottom:14px}.req input{width:90px}"
        "table.res{width:100%%;border-collapse:collapse;font-size:.92rem}"
        ".res th{text-align:left;font-size:.78rem;color:#5b6478;padding:6px 4px}"
        ".res td{padding:8px 4px;border-top:1px solid #e3e7ef;vertical-align:top}"
        ".safe{color:#0f7b3f;font-weight:600}.edge{color:#a16207;font-weight:600}.short{color:#b91c1c;font-weight:600}"
        ".small{font-size:.85rem;color:#5b6478}.err{color:#b91c1c}"
        "</style></head><body><div class=\"wrap\">", title);

    /* Team banner - shown on every page */
    bprintf(b, "<div class=\"team\"><h1>");
    bescape(b, TEAM_NAME);
    bprintf(b, "</h1><p>FOCP Assignment &mdash; Attendance Planner</p>"
               "<table><tr><th>Member</th><th>Scholar No.</th></tr>");
    for (size_t i = 0; i < N_MEMBERS; i++) {
        bprintf(b, "<tr><td>");
        bescape(b, MEMBERS[i][0]);
        bprintf(b, "</td><td>");
        bescape(b, MEMBERS[i][1]);
        bprintf(b, "</td></tr>");
    }
    bprintf(b, "</table></div>");
}

static void page_end(Buf *b)
{
    bprintf(b, "<p class=\"small\">Written in C. No libraries, no frameworks.</p></div></body></html>");
}

/* Example data so a visitor can try it with one tap. */
static const struct { const char *name; int held, att; } DEMO[] = {
    {"Mathematics", 40, 34}, {"Physics", 38, 26}, {"Programming (FOCP)", 30, 29},
    {"Chemistry", 36, 27},   {"English", 20, 14},
};
#define N_DEMO (sizeof DEMO / sizeof DEMO[0])

static void build_home(Buf *b, int demo)
{
    page_start(b, "Attendance Planner");
    bprintf(b,
        "<div class=\"card\"><h2>The problem</h2>"
        "<p>Most colleges need <b>75%%</b> attendance to sit in exams. Students keep "
        "guessing: <i>&ldquo;Can I skip tomorrow's class?&rdquo;</i> or "
        "<i>&ldquo;How many classes must I attend to be safe again?&rdquo;</i> "
        "Enter your numbers below and get the exact answer for each subject.</p></div>"
        "<form class=\"card\" method=\"POST\" action=\"/calc\">"
        "<div class=\"req\"><label for=\"target\"><b>Required attendance %%</b></label>"
        "<input id=\"target\" name=\"target\" type=\"number\" min=\"1\" max=\"100\" value=\"75\"></div>"
        "<div class=\"row head\"><div>Subject</div><div>Classes held</div><div>Attended</div></div>");

    for (int i = 0; i < MAX_SUBJECTS; i++) {
        bprintf(b, "<div class=\"row\">"
                   "<input name=\"n%d\" maxlength=\"%d\" placeholder=\"Subject %d\" value=\"",
                i, NAME_LEN - 1, i + 1);
        if (demo && i < (int)N_DEMO) bescape(b, DEMO[i].name);
        bprintf(b, "\"><input name=\"h%d\" type=\"number\" min=\"0\" max=\"1000\" inputmode=\"numeric\" value=\"", i);
        if (demo && i < (int)N_DEMO) bprintf(b, "%d", DEMO[i].held);
        bprintf(b, "\"><input name=\"a%d\" type=\"number\" min=\"0\" max=\"1000\" inputmode=\"numeric\" value=\"", i);
        if (demo && i < (int)N_DEMO) bprintf(b, "%d", DEMO[i].att);
        bprintf(b, "\"></div>");
    }
    bprintf(b,
        "<p class=\"small\">Leave unused rows empty.</p>"
        "<button type=\"submit\">Calculate</button> "
        "<a class=\"btn\" style=\"background:#64748b\" href=\"/?demo=1\">Load example</a>"
        "</form>");
    page_end(b);
}

static void build_result(Buf *b, const char *body)
{
    Subject subs[MAX_SUBJECTS];
    int count = 0, target = 75;
    int tot_held = 0, tot_att = 0;
    char field[NAME_LEN + 16], key[8];
    char errors[512] = "";

    if (form_get(body, "target", field, sizeof field))
        if (!parse_int(field, 1, 100, &target)) target = 75;

    for (int i = 0; i < MAX_SUBJECTS; i++) {
        char hs[16], as[16], nm[NAME_LEN + 16];

        snprintf(key, sizeof key, "n%d", i); form_get(body, key, nm, sizeof nm);
        snprintf(key, sizeof key, "h%d", i); form_get(body, key, hs, sizeof hs);
        snprintf(key, sizeof key, "a%d", i); form_get(body, key, as, sizeof as);

        if (hs[0] == '\0' && as[0] == '\0') continue;           /* empty row */

        Subject *s = &subs[count];
        if (nm[0] == '\0') snprintf(s->name, NAME_LEN, "Subject %d", i + 1);
        else               snprintf(s->name, NAME_LEN, "%.*s", NAME_LEN - 1, nm);

        if (!parse_int(hs, 1, 1000, &s->held) ||
            !parse_int(as, 0, 1000, &s->attended) ||
            s->attended > s->held) {
            size_t used = strlen(errors);
            snprintf(errors + used, sizeof errors - used,
                     "Row %d: enter classes held (1 or more) and attended (not more than held).<br>", i + 1);
            continue;
        }
        analyse(s, target);
        tot_held += s->held;
        tot_att  += s->attended;
        count++;
    }

    page_start(b, "Your result");
    bprintf(b, "<div class=\"card\"><h2>Your result (need %d%%)</h2>", target);

    if (errors[0]) bprintf(b, "<p class=\"err\">%s</p>", errors);
    if (count == 0) {
        bprintf(b, "<p>Nothing to calculate yet. Go back and fill at least one subject.</p>");
    } else {
        bprintf(b, "<table class=\"res\"><tr><th>Subject</th><th>Att.</th><th>%%</th><th>What to do</th></tr>");
        for (int i = 0; i < count; i++) {
            Subject *s = &subs[i];
            const char *cls;
            char msg[160];
            if (s->must_attend == -1) {
                cls = "short";
                snprintf(msg, sizeof msg, "Cannot reach %d%% any more", target);
            } else if (s->percent < target) {
                cls = "short";
                snprintf(msg, sizeof msg, "SHORT: attend the next %d class%s in a row",
                         s->must_attend, s->must_attend == 1 ? "" : "es");
            } else if (s->can_miss == 0) {
                cls = "edge";
                snprintf(msg, sizeof msg, "On the edge: do not miss the next class");
            } else {
                cls = "safe";
                snprintf(msg, sizeof msg, "SAFE: you can miss %d more class%s",
                         s->can_miss, s->can_miss == 1 ? "" : "es");
            }
            bprintf(b, "<tr><td>");
            bescape(b, s->name);
            bprintf(b, "</td><td>%d/%d</td><td>%.1f</td><td class=\"%s\">%s</td></tr>",
                    s->attended, s->held, s->percent, cls, msg);
        }
        Subject all = { "Overall", tot_held, tot_att, 0, 0, 0 };
        analyse(&all, target);
        bprintf(b, "</table><p class=\"small\">Overall: %d/%d classes = <b>%.1f%%</b></p>",
                tot_att, tot_held, all.percent);
        bprintf(b, "<p class=\"small\">Assumes every future class is held and you either attend or skip it.</p>");
    }
    bprintf(b, "<a class=\"btn\" href=\"/\">Calculate again</a></div>");
    page_end(b);
}

/* ---------- Tiny HTTP server ---------- */
static void send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n <= 0) return;
        data += n;
        len  -= (size_t)n;
    }
}

static void respond(int fd, const char *status, const char *ctype, const char *body, size_t len)
{
    char head[256];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, ctype, len);
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

static void handle_client(int fd)
{
    static char req[REQ_MAX + 1];     /* static: each forked child gets its own copy */
    size_t got = 0;
    char *hdr_end = NULL;

    /* 1. read until the blank line that ends the headers */
    while (got < REQ_MAX && !hdr_end) {
        ssize_t n = recv(fd, req + got, REQ_MAX - got, 0);
        if (n <= 0) return;
        got += (size_t)n;
        req[got] = '\0';
        hdr_end = strstr(req, "\r\n\r\n");
    }
    if (!hdr_end) return;
    size_t body_off = (size_t)(hdr_end - req) + 4;

    /* 2. method and path, e.g.  "GET /health" */
    char method[8] = "", path[256] = "";
    sscanf(req, "%7s %255s", method, path);

    /* 3. if there is a body (POST), read all of it */
    size_t content_len = 0;
    char *cl = strcasestr(req, "Content-Length:");
    if (cl && cl < hdr_end) content_len = (size_t)atol(cl + 15);
    if (body_off + content_len > REQ_MAX) {
        static const char msg[] = "Request too large";
        respond(fd, "413 Payload Too Large", "text/plain", msg, sizeof msg - 1);
        return;
    }
    while (got < body_off + content_len) {
        ssize_t n = recv(fd, req + got, REQ_MAX - got, 0);
        if (n <= 0) break;
        got += (size_t)n;
    }
    req[got] = '\0';
    const char *body = req + body_off;

    /* 4. route the request */
    static Buf page;                   /* static: too big for the stack */
    page.len = 0;
    const char *html = "text/html; charset=utf-8";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        respond(fd, "200 OK", "text/plain", "ok", 2);
    } else if (strcmp(method, "GET") == 0 && (path[1] == '\0' || path[1] == '?') && path[0] == '/') {
        build_home(&page, strstr(path, "demo=1") != NULL);
        respond(fd, "200 OK", html, page.data, page.len);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/calc") == 0) {
        build_result(&page, body);
        respond(fd, "200 OK", html, page.data, page.len);
    } else {
        static const char nf[] = "404 - page not found";
        respond(fd, "404 Not Found", "text/plain", nf, sizeof nf - 1);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);          /* finished children are cleaned up automatically */

    const char *port_env = getenv("PORT");   /* hosting sites tell us the port this way */
    int port = port_env ? atoi(port_env) : 8080;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((unsigned short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(srv, 32) < 0) { perror("listen"); return 1; }
    printf("Attendance Planner running on port %d\n", port);
    fflush(stdout);

    for (;;) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;

        struct timeval tv = { 5, 0 };  /* give up on silent connections after 5 s */
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        pid_t pid = fork();            /* one child per visitor, so nobody waits in line */
        if (pid == 0) {
            close(srv);
            handle_client(fd);
            close(fd);
            _exit(0);
        }
        if (pid < 0) handle_client(fd);  /* fork failed: serve it ourselves */
        close(fd);
    }
}
