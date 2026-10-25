/* cheat.c: test-only cheats (cheat.h). An input script's line

       T cheat COMMAND ARGS...

   asks for COMMAND, one of the game's own (its struct cheat_game), at time T. The script is
   refused unless the environment has EXHUME_TEST_CHEATS=1, and the web build refuses it always,
   so that no script passed to a player's build can change its game; the line is checked when
   the script is read, as every other line is. Its time only lets it go: the cheat runs on the
   game's thread at the next safe point (port_cheat_point, once a pass of the game's main loop)
   at which the game says it may (ready: in the game, on its 3D view, nothing pending), so it
   changes the game between two of its ticks, through the game's own routines, never under
   them.

   A session that used a cheat says so: each run logs "cheat: TEXT (not as DOS)", and a
   recording made with cheats in its script is format 6 (runtime/replay/replay.c): format 5's
   enhancements chunk, always there, and each cheat as a chunk of stream 10 where it ran, the
   number of the safe point it ran at (a dword, counting from 1) and its text. A replay counts
   the same safe points, the game taking the same path, and runs each cheat again at its own;
   it too needs EXHUME_TEST_CHEATS=1 (tools/replay.py sets it for a format 6 recording), and
   DOS, which has no cheats, never replays one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "cheat.h"
#include "portgame.h"
#ifndef PORT_NAME
#define PORT_NAME "port"
#endif

#define MAXCHEATS 4096
#define CHEAT_LEN 128
#define MAXARGS 12

static const struct cheat_game *game;
static const struct cheat_game *menu;
#define MENU_QUEUE 32
static char menu_queue[MENU_QUEUE][CHEAT_LEN];
static int menu_read, menu_write;
static atomic_flag menu_busy = ATOMIC_FLAG_INIT;
static void menu_lock(void) { while (atomic_flag_test_and_set_explicit(&menu_busy, memory_order_acquire)) {} }
static void menu_unlock(void) { atomic_flag_clear_explicit(&menu_busy, memory_order_release); }
static char text[MAXCHEATS][CHEAT_LEN];
static uint32_t at[MAXCHEATS];          /* replaying: the safe point each runs at */
static int ncheats, done;
static int replaying;                   /* the cheats are a recording's, not the script's */
static atomic_int due;                  /* the script's cheats whose time has come */
static uint32_t polls;                  /* the safe points so far */
static char why[256];

void cheat_init(const struct cheat_game *g) { game = g; }
void cheat_menu_init(const struct cheat_game *m) { menu = m; }
int cheat_menu_request(const char *t)
{
    int next;
    if (!menu || !t || strlen(t) >= CHEAT_LEN || replaying) return -1;
    menu_lock(); next = (menu_write + 1) % MENU_QUEUE;
    if (next == menu_read) { menu_unlock(); return -1; }
    strcpy(menu_queue[menu_write], t); menu_write = next; menu_unlock(); return 0;
}

int cheat_allowed(void)
{
#ifdef __EMSCRIPTEN__
    return 0;
#else
    const char *on = getenv("EXHUME_TEST_CHEATS");
    return on && !strcmp(on, "1");
#endif
}

int cheat_num(const char *s, long lo, long hi, long *v)
{
    char *end;
    const char *from;
    long n;
    if (!s || !*s) return -1;
    from = s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? s + 2 : s;
    if (*from == '-' || *from == '+' || *from == ' ') {
        if (from != s || *from != '-') return -1;    /* a sign only before a decimal */
    }
    n = strtol(from, &end, from == s ? 10 : 16);
    if (*end || end == from || n < lo || n > hi) return -1;
    *v = n;
    return 0;
}

/* LINE's words into argv (LINE is changed); how many, or -1 for too many */
static int split(char *line, char *argv[])
{
    int n = 0;
    char *p = strtok(line, " \t\r\n");
    while (p) {
        if (n == MAXARGS) return -1;
        argv[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    return n;
}

/* T's command, checked (run 0) or run (run 1): NULL, or what is wrong */
static const char *apply_table(const struct cheat_game *table, const char *t, int run)
{
    char buf[CHEAT_LEN], *argv[MAXARGS];
    const char *e;
    int argc, i;
    if (!table) return "this build has no cheats";
    if (strlen(t) >= sizeof buf) return "too long";
    strcpy(buf, t);
    if ((argc = split(buf, argv)) < 0) return "too many words";
    if (argc == 0) return "a command";
    for (i = 0; i < table->ncmds; i++)
        if (!strcmp(table->cmds[i].name, argv[0])) break;
    if (i == table->ncmds) {
        size_t k = (size_t)snprintf(why, sizeof why, "no such cheat %s (", argv[0]);
        for (i = 0; i < table->ncmds && k < sizeof why; i++)
            k += (size_t)snprintf(why + k, sizeof why - k, "%s%s", i ? ", " : "", table->cmds[i].name);
        if (k < sizeof why) snprintf(why + k, sizeof why - k, ")");
        return why;
    }
    if ((e = table->cmds[i].fn(argc - 1, argv + 1, run)) != NULL) {
        snprintf(why, sizeof why, "%s: %s (%s %s)", argv[0], e, argv[0], table->cmds[i].usage);
        return why;
    }
    return NULL;
}
static const char *apply(const char *t, int run)
{
    if (menu && !apply_table(menu, t, 0)) return apply_table(menu, t, run);
    return apply_table(game, t, run);
}

const char *cheat_add(const char *t)
{
    const char *e;
    while (*t == ' ' || *t == '\t') t++;
    if (!cheat_allowed())
#ifdef __EMSCRIPTEN__
        return "cheat: not in the web build";
#else
        return "cheat: a test's event, refused unless EXHUME_TEST_CHEATS=1";
#endif
    if (ncheats == MAXCHEATS) return "cheat: too many cheats";
    if ((e = apply(t, 0)) != NULL) return e;
    {   /* the text as recorded and logged: its words, one space apart */
        char buf[CHEAT_LEN], *argv[MAXARGS];
        int i, argc;
        strcpy(buf, t);                 /* apply has checked its length and its words */
        argc = split(buf, argv);
        text[ncheats][0] = 0;
        for (i = 0; i < argc; i++) {
            if (i) strcat(text[ncheats], " ");
            strcat(text[ncheats], argv[i]);
        }
    }
    ncheats++;
    return NULL;
}

void cheat_release(void) { atomic_fetch_add(&due, 1); }

int cheat_scripted(void) { return replaying ? 0 : ncheats; }

/* Format 6: the header (12 bytes, the version at 4), then the chunks, each a stream number
   (byte), a length (word) and the bytes; stream 10's are the cheats. */
int cheat_from_recording(const char *path)
{
    unsigned char h[12], c[3], b[4 + CHEAT_LEN];
    unsigned n;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;                   /* the replay itself says it cannot open it */
    if (fread(h, 1, 12, f) != 12 || h[4] != 6) {
        fclose(f);
        return 0;
    }
    while (fread(c, 1, 3, f) == 3) {
        n = c[1] | (unsigned)c[2] << 8;
        if (c[0] != 10) {
            if (fseek(f, (long)n, SEEK_CUR)) break;
            continue;
        }
        if (n < 5 || n > sizeof b - 1 || fread(b, 1, n, f) != n || ncheats == MAXCHEATS) {
            fclose(f);
            fprintf(stderr, PORT_NAME ": cheat: %s: a bad cheat chunk\n", path);
            return -1;
        }
        at[ncheats] = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        memcpy(text[ncheats], b + 4, n - 4);
        text[ncheats][n - 4] = 0;
        if (!cheat_allowed() && apply_table(menu, text[ncheats], 0)) {
            fclose(f);
            fprintf(stderr, PORT_NAME ": cheat: test commands require EXHUME_TEST_CHEATS=1\n");
            return -1;
        }
        ncheats++;
    }
    fclose(f);
    replaying = 1;
    return 0;
}

static void run_one(const char *t)
{
    const char *e = apply(t, 1);
    if (e) fprintf(stderr, PORT_NAME ": cheat: %s: %s (not as DOS)\n", t, e);
    else fprintf(stderr, PORT_NAME ": cheat: %s (not as DOS)\n", t);
}

void port_cheat_point(void)
{
    if (!game) return;
    polls++;
    if (replaying) {
        while (done < ncheats && at[done] == polls) run_one(text[done++]);
        return;
    }
    while (done < atomic_load(&due) && game->ready()) {
        rp_cheat_put(polls, text[done]);
        run_one(text[done++]);
    }
    if (menu && menu->ready()) {
        char request[CHEAT_LEN];
        int have;
        menu_lock(); have = menu_read != menu_write;
        if (have) { strcpy(request, menu_queue[menu_read]); menu_read = (menu_read + 1) % MENU_QUEUE; }
        menu_unlock();
        if (have) {
            if (!apply_table(menu, request, 0)) { rp_cheat_put(polls, request); apply_table(menu, request, 1); }
            else fprintf(stderr, PORT_NAME ": menu: refused command\n");
        }
    }
}
