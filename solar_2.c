/*
 * solar_sdl.c — a 2D solar system rendered with SDL2
 *
 * Built in the same style as the classic SDL2 ray-casting tutorial:
 * a Circle struct, a FillCircle() that paints with SDL_FillRect, and
 * sun rays that march pixel by pixel — except here the rays are blocked
 * by EIGHT moving planets, so every planet casts a real-time shadow.
 *
 * install: sudo apt install libsdl2-dev
 * build:   gcc solar_sdl.c -o solar_sdl -lSDL2 -lm
 * run:     ./solar_sdl
 *
 * controls:
 *   drag mouse  — move the sun (planets + shadows follow)
 *   space       — pause
 *   + / -       — time speed
 *   r           — toggle sun rays
 *   o           — toggle orbit rings
 *   q / Esc     — quit
 */

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH         1200
#define HEIGHT        600
#define NSTARS        220
#define RAYS_NUMBER   360
#define RAY_STEP      1.0
#define RAY_THICKNESS 1
#define NPLANETS      8
#define KEPLER_K      1.45   /* overall pace of the orbits */

struct Circle {
    double x;
    double y;
    double r;
};

struct Planet {
    const char *name;
    double orbit_r;          /* orbit radius in pixels             */
    double angle;            /* current position (radians)         */
    double radius;           /* drawn size in pixels               */
    Uint8  cr, cg, cb;       /* colour                             */
    int    ringed;           /* Saturn gets an ellipse ring        */
    double px, py;           /* screen position, updated per frame */
};

static struct Planet planets[NPLANETS] = {
    { "Mercury",  60, 0.0,  3.0, 180, 180, 180, 0, 0, 0 },
    { "Venus",    92, 1.7,  5.0, 235, 190, 110, 0, 0, 0 },
    { "Earth",   124, 3.1,  5.5,  80, 140, 235, 0, 0, 0 },
    { "Mars",    152, 4.4,  4.0, 215,  90,  55, 0, 0, 0 },
    { "Jupiter", 190, 5.5, 11.0, 225, 160,  95, 0, 0, 0 },
    { "Saturn",  222, 0.9,  9.0, 235, 205, 140, 1, 0, 0 },
    { "Uranus",  252, 2.6,  7.0, 140, 220, 230, 0, 0, 0 },
    { "Neptune", 280, 4.0,  7.0,  80, 110, 235, 0, 0, 0 },
};

struct Star { double x, y, base, phase; };
static struct Star stars[NSTARS];

/*
 * Same idea as testing every pixel against x²+y² ≤ r², but solved for x:
 * on each row dy the circle spans ±sqrt(r²−dy²), so the whole row can be
 * painted with ONE SDL_FillRect instead of one call per pixel. Same
 * result as the per-pixel version, hundreds of times fewer calls.
 */
void FillCircle(SDL_Surface *surface, struct Circle circle, Uint32 color)
{
    double radius_squared = circle.r * circle.r;
    for (double dy = -circle.r; dy <= circle.r; dy += 1.0) {
        double half = sqrt(radius_squared - dy * dy);
        SDL_Rect row = { (int)(circle.x - half), (int)(circle.y + dy),
                         (int)(half * 2.0) + 1, 1 };
        SDL_FillRect(surface, &row, color);
    }
}

/* circle/ellipse outline from parametric points; gap>1 makes it dotted */
void DrawRing(SDL_Surface *surface, double cx, double cy,
              double rx, double ry, int gap, Uint32 color)
{
    int steps = (int)(2.0 * M_PI * ((rx + ry) / 2.0));
    if (steps < 8) steps = 8;
    for (int i = 0; i < steps; i += gap) {
        double a = 2.0 * M_PI * i / steps;
        SDL_Rect p = { (int)(cx + rx * cos(a)), (int)(cy + ry * sin(a)),
                       1, 1 };
        SDL_FillRect(surface, &p, color);
    }
}

/*
 * March each ray outward from the sun's edge one step at a time —
 * exactly like the tutorial — but test the ray tip against every
 * planet. The moment it lands inside one, the ray stops: that is
 * what creates the shadow cone behind each planet.
 */
void DrawRays(SDL_Surface *surface, struct Circle sun, Uint32 color)
{
    for (int i = 0; i < RAYS_NUMBER; i++) {
        double angle = 2.0 * M_PI * i / RAYS_NUMBER;
        double dx = cos(angle), dy = sin(angle);
        double x = sun.x + dx * (sun.r + 2.0);
        double y = sun.y + dy * (sun.r + 2.0);

        int object_hit = 0;
        while (!object_hit &&
               x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
            SDL_Rect ray_point = { (int)x, (int)y,
                                   RAY_THICKNESS, RAY_THICKNESS };
            SDL_FillRect(surface, &ray_point, color);

            x += dx * RAY_STEP;
            y += dy * RAY_STEP;

            for (int k = 0; k < NPLANETS; k++) {
                double ddx = x - planets[k].px;
                double ddy = y - planets[k].py;
                if (ddx * ddx + ddy * ddy <=
                    planets[k].radius * planets[k].radius) {
                    object_hit = 1;
                    break;
                }
            }
        }
    }
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Solar System",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
    if (!window) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface *surface = SDL_GetWindowSurface(window);

    /*
     * SDL_MapRGB asks the surface "how do YOU store this colour?" —
     * safer than raw hex like 0xffd43b, which silently assumes a
     * particular byte order and breaks on other pixel formats.
     */
    Uint32 C_BG    = SDL_MapRGB(surface->format,   8,   8,  14);
    Uint32 C_SUN   = SDL_MapRGB(surface->format, 255, 212,  59);
    Uint32 C_GLOW1 = SDL_MapRGB(surface->format, 120,  78,  18);
    Uint32 C_GLOW2 = SDL_MapRGB(surface->format,  56,  38,  10);
    Uint32 C_RAY   = SDL_MapRGB(surface->format,  64,  48,  16);
    Uint32 C_ORBIT = SDL_MapRGB(surface->format,  55,  58,  70);
    Uint32 C_RINGS = SDL_MapRGB(surface->format, 200, 180, 140);

    struct Circle sun = { WIDTH / 2.0, HEIGHT / 2.0, 26.0 };

    srand(42);
    for (int i = 0; i < NSTARS; i++) {
        stars[i].x     = rand() % WIDTH;
        stars[i].y     = rand() % HEIGHT;
        stars[i].base  = 90 + rand() % 130;
        stars[i].phase = (rand() % 628) / 100.0;
    }

    int running = 1, paused = 0, show_rays = 1, show_orbits = 1;
    double speed = 1.0, t = 0.0;
    Uint32 last = SDL_GetTicks();
    SDL_Event event;

    while (running) {
        /* ---------- input ---------- */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:        running = 0;             break;
                    case SDLK_SPACE:    paused = !paused;        break;
                    case SDLK_r:        show_rays = !show_rays;  break;
                    case SDLK_o:        show_orbits = !show_orbits; break;
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        speed *= 1.5; if (speed > 12.0) speed = 12.0; break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        speed /= 1.5; if (speed < 0.1) speed = 0.1;   break;
                }
            } else if (event.type == SDL_MOUSEMOTION &&
                       (event.motion.state & SDL_BUTTON_LMASK)) {
                sun.x = event.motion.x;      /* drag the sun around */
                sun.y = event.motion.y;
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                       event.button.button == SDL_BUTTON_LEFT) {
                sun.x = event.button.x;
                sun.y = event.button.y;
            }
        }

        /* ---------- physics ---------- */
        Uint32 now = SDL_GetTicks();
        double dt = (now - last) / 1000.0;   /* frame-rate independent */
        last = now;
        if (dt > 0.05) dt = 0.05;
        t += dt;

        if (!paused) {
            for (int i = 0; i < NPLANETS; i++) {
                /* Kepler's third law: angular speed ∝ r^(-3/2) */
                double omega = KEPLER_K *
                               pow(planets[i].orbit_r / 100.0, -1.5);
                planets[i].angle += omega * dt * speed;
            }
        }
        for (int i = 0; i < NPLANETS; i++) {
            planets[i].px = sun.x + planets[i].orbit_r * cos(planets[i].angle);
            planets[i].py = sun.y + planets[i].orbit_r * sin(planets[i].angle);
        }

        /* ---------- draw ---------- */
        SDL_FillRect(surface, NULL, C_BG);

        for (int i = 0; i < NSTARS; i++) {           /* twinkling stars */
            int b = (int)(stars[i].base +
                          50.0 * sin(t * 1.3 + stars[i].phase));
            if (b < 30)  b = 30;
            if (b > 255) b = 255;
            int sz = (i % 13 == 0) ? 2 : 1;
            SDL_Rect p = { (int)stars[i].x, (int)stars[i].y, sz, sz };
            SDL_FillRect(surface, &p,
                         SDL_MapRGB(surface->format, b, b, b));
        }

        /* soft glow behind the sun */
        FillCircle(surface,
                   (struct Circle){ sun.x, sun.y, sun.r * 2.1 }, C_GLOW2);
        FillCircle(surface,
                   (struct Circle){ sun.x, sun.y, sun.r * 1.45 }, C_GLOW1);

        if (show_rays)
            DrawRays(surface, sun, C_RAY);

        if (show_orbits)
            for (int i = 0; i < NPLANETS; i++)
                DrawRing(surface, sun.x, sun.y,
                         planets[i].orbit_r, planets[i].orbit_r, 6, C_ORBIT);

        FillCircle(surface, sun, C_SUN);

        for (int i = 0; i < NPLANETS; i++) {
            FillCircle(surface,
                       (struct Circle){ planets[i].px, planets[i].py,
                                        planets[i].radius },
                       SDL_MapRGB(surface->format, planets[i].cr,
                                  planets[i].cg, planets[i].cb));
            if (planets[i].ringed)                    /* Saturn's ring */
                DrawRing(surface, planets[i].px, planets[i].py,
                         planets[i].radius * 2.1, planets[i].radius * 0.8,
                         1, C_RINGS);
        }

        SDL_UpdateWindowSurface(window);
        SDL_Delay(16);                                /* ~60 fps */
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
