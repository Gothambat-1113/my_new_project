0#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ASPECT   2.05    /* cell height : width ratio                */
#define FRAME_US 33000   /* ~30 frames per second                    */
#define BASE_DT  0.033   /* simulated seconds per frame at speed x1  */
#define KEPLER_K 0.35    /* overall pace of the orbits               */

enum { GL_BLOCK = 1, GL_DOT = 2 };

typedef struct {
    const char *name;
    double      frac;    /* orbit radius as a fraction of the max radius */
    double      angle;   /* current position on the orbit (radians)      */
    int         colour;  /* xterm-256 colour index                       */
    char        glyph;
    int         ringed;  /* Saturn gets <O>                              */
} Planet;

static Planet planets[] = {
    { "Mercury", 0.16, 0.0, 250, 'o', 0 },
    { "Venus",   0.26, 1.7, 220, 'o', 0 },
    { "Earth",   0.36, 3.1,  39, 'o', 0 },
    { "Mars",    0.46, 4.4, 196, 'o', 0 },
    { "Jupiter", 0.60, 5.5, 208, 'O', 0 },
    { "Saturn",  0.72, 0.9, 222, 'O', 1 },
    { "Uranus",  0.84, 2.6,  51, 'o', 0 },
    { "Neptune", 0.95, 4.0,  27, 'o', 0 },
};
#define NPLANETS (int)(sizeof planets / sizeof planets[0])

static volatile sig_atomic_t running = 1;
static struct termios saved_tio;
static int            tio_saved = 0;

static void handle_signal(int sig) { (void)sig; running = 0; }

static void restore_terminal(void)
{
    if (tio_saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    fputs("\033[0m\033[?25h\033[2J\033[H", stdout); /* reset, cursor on */
    fflush(stdout);
}

static void raw_mode(void)
{
    struct termios t;
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &saved_tio) != 0) return;
    tio_saved = 1;
    t = saved_tio;
    t.c_lflag &= ~(ICANON | ECHO);  /* keys arrive instantly, no echo */
    t.c_cc[VMIN]  = 0;              /* read() never blocks            */
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

static char          *chbuf  = NULL;  /* glyph per cell                */
static unsigned char *cobuf  = NULL;  /* colour per cell               */
static char          *outbuf = NULL;  /* assembled escape-code stream  */
static int            brows  = 0, bcols = 0;

static void ensure_buffers(int rows, int cols)
{
    if (rows == brows && cols == bcols) return;
    free(chbuf); free(cobuf); free(outbuf);
    brows = rows; bcols = cols;
    chbuf  = calloc((size_t)rows * cols, 1);
    cobuf  = calloc((size_t)rows * cols, 1);
    outbuf = malloc((size_t)rows * cols * 16 + rows + 64);
    if (!chbuf || !cobuf || !outbuf) { perror("malloc"); exit(1); }
}

static void put(int x, int y, char ch, int colour)
{
    if (x < 0 || x >= bcols || y < 0 || y >= brows) return;
    chbuf[y * bcols + x] = ch;
    cobuf[y * bcols + x] = (unsigned char)colour;
}

static void put_text(int x, int y, const char *s, int colour)
{
    for (; *s; s++, x++) put(x, y, *s, colour);
}

typedef struct { int x, y; double phase; } Star;
static Star *stars  = NULL;
static int   nstars = 0;

static void make_stars(int rows, int cols)
{
    free(stars);
    nstars = rows * cols / 90;
    stars  = malloc(sizeof(Star) * (size_t)nstars);
    if (!stars) { perror("malloc"); exit(1); }
    srand(1234);
    for (int i = 0; i < nstars; i++) {
        stars[i].x = rand() % cols;
        stars[i].y = rand() % rows;
        stars[i].phase = (rand() % 628) / 100.0;
    }
}
static void draw_frame(double t, int rows, int cols,
                       double speed, int paused, int labels)
{
    memset(chbuf, 0, (size_t)rows * cols);
    memset(cobuf, 0, (size_t)rows * cols);

    /* stars, with a slow twinkle driven by a per-star phase */
    for (int i = 0; i < nstars; i++) {
        double tw = sin(t * 0.8 + stars[i].phase);
        int c = tw > 0.6 ? 250 : (tw > -0.3 ? 244 : 238);
        put(stars[i].x, stars[i].y, (i % 7 == 0) ? '+' : '.', c);
    }

    double cx = cols / 2.0, cy = rows / 2.0;
    /* biggest radius that fits both vertically and horizontally */
    double maxr = fmin((rows - 2) / 2.0 - 1.0, (cols / 2.0 - 2.0) / ASPECT);
    if (maxr < 3) maxr = 3;

    /* orbit rings (only on empty cells, so they sit behind the stars) */
    for (int p = 0; p < NPLANETS; p++) {
        double r = planets[p].frac * maxr;
        double step = 1.0 / (r * ASPECT + 6.0);
        for (double a = 0; a < 2 * M_PI; a += step) {
            int x = (int)lround(cx + r * ASPECT * cos(a));
            int y = (int)lround(cy + r * sin(a));
            if (x >= 0 && x < cols && y >= 0 && y < rows &&
                chbuf[y * cols + x] == 0)
                put(x, y, GL_DOT, 238);
        }
    }
double rs = fmax(1.1, fmin(2.0, rows * 0.05));
    for (int dy = (int)(-rs) - 1; dy <= (int)rs + 1; dy++)
        for (int dx = (int)(-rs * ASPECT) - 1; dx <= (int)(rs * ASPECT) + 1; dx++) {
            double d = sqrt((dx / ASPECT) * (dx / ASPECT) + (double)dy * dy);
            if (d <= rs)
                put((int)cx + dx, (int)cy + dy, GL_BLOCK,
                    d < rs * 0.55 ? 226 : 208);
        }

 for (int p = 0; p < NPLANETS; p++) {
        Planet *pl = &planets[p];
        double r = pl->frac * maxr;
        int x = (int)lround(cx + r * ASPECT * cos(pl->angle));
        int y = (int)lround(cy + r * sin(pl->angle));
        if (pl->ringed) {
            put(x - 1, y, '<', 180);
            put(x,     y, pl->glyph, pl->colour);
            put(x + 1, y, '>', 180);
        } else {
            put(x, y, pl->glyph, pl->colour);
        }
        if (labels) {
            int len = (int)strlen(pl->name);
            int lx = (x + 2 + len <= cols) ? x + 2 : x - len - 1;
            put_text(lx, y, pl->name, 244);
        }
    }
char status[128];
    snprintf(status, sizeof status,
             " solar system  x%.2g %s  q quit | space pause | +/- speed | l labels ",
             speed, paused ? "(paused)" : "");
    put_text(1, 0, status, 240);
}
static void render(int rows, int cols)
{
    size_t o = 0;
    int last = -1;
    memcpy(outbuf + o, "\033[H", 3); o += 3;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            char c = chbuf[y * cols + x];
            if (c == 0) { outbuf[o++] = ' '; continue; }
            int col = cobuf[y * cols + x];
            if (col != last) {
                o += (size_t)sprintf(outbuf + o, "\033[38;5;%dm", col);
                last = col;
            }
            if (c == GL_BLOCK)    { memcpy(outbuf + o, "\xE2\x96\x88", 3); o += 3; }
            else if (c == GL_DOT) { memcpy(outbuf + o, "\xC2\xB7", 2);     o += 2; }
            else                  outbuf[o++] = c;
        }
        if (y < rows - 1) outbuf[o++] = '\n';
    }
    fwrite(outbuf, 1, o, stdout);
    fflush(stdout);
}
int main(void)
{
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    raw_mode();
    atexit(restore_terminal);

    fputs("\033[2J\033[?25l", stdout);  /* clear once, hide cursor */

    double t = 0.0, speed = 1.0;
    int paused = 0, labels = 1;
    int rows = 0, cols = 0;

    while (running) {
        int r, c;
        term_size(&r, &c);
        if (r != rows || c != cols) {      /* live resize support */
            rows = r; cols = c;
            ensure_buffers(rows, cols);
            make_stars(rows, cols);
            fputs("\033[2J", stdout);
        }
char k;
        while (read(STDIN_FILENO, &k, 1) == 1) {
            if (k == 'q' || k == 'Q') running = 0;
            else if (k == ' ') paused = !paused;
            else if (k == '+' || k == '=') { speed *= 1.5; if (speed > 12)  speed = 12; }
            else if (k == '-' || k == '_') { speed /= 1.5; if (speed < 0.1) speed = 0.1; }
            else if (k == 'l' || k == 'L') labels = !labels;
        }
 double dt = paused ? 0.0 : BASE_DT * speed;
        for (int p = 0; p < NPLANETS; p++)
            planets[p].angle += KEPLER_K * pow(planets[p].frac, -1.5) * dt;

        t += BASE_DT;  /* twinkle clock keeps running even when paused */

        draw_frame(t, rows, cols, speed, paused, labels);
        render(rows, cols);
        usleep(FRAME_US);
    }
    return 0;
}
