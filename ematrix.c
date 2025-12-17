// Build: gcc -O2 -Wall -Wextra ematrix.c -lncurses -lm -o ematrix
// Keys: q to quit

#include <ncurses.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

typedef struct {
  float vx0, vy0;     // initial vector (relative to center)
  float born;         // birth time (seconds)
  char  ch;           // character to draw
} Particle;

static float frandf(float a, float b) {
  return a + (b - a) * (float)rand() / (float)RAND_MAX;
}

static char rand_char(void) {
  static const char *set =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@#$%&*+=-";
  return set[rand() %  (int)strlen(set)];
}

// Analytic matrix exponential for A = [[-1,-1],[1,0]].
// exp(A t) = e^{-0.5 t} [ cos(w t) I + (sin(w t)/w) (A + 0.5 I) ]
// where w = sqrt(3)/2 ≈ 0.8660254
static void expA(float t, float M[2][2]) {
  const float w = 0.8660254037844386f; // sqrt(3)/2
  float et = expf(-0.5f * t);
  float c  = cosf(w * t);
  float s  = sinf(w * t);
  float k  = (fabsf(w) < 1e-8f) ? t : (s / w);

  // B = A + 0.5 I = [[-0.5, -1],[1, 0.5]]
  float B00 = -0.5f, B01 = -1.0f, B10 = 1.0f, B11 = 0.5f;

  // M = et * ( c*I + k*B )
  M[0][0] = et * (c + k * B00);
  M[0][1] = et * (    k * B01);
  M[1][0] = et * (    k * B10);
  M[1][1] = et * (c + k * B11);
}

static float now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (float)ts.tv_sec + (float)ts.tv_nsec * 1e-9f;
}

static void respawn(Particle *p, int cols, int rows) {
  // Spawn somewhere in a ring around the center
  float cx = (cols - 1) * 0.5f;
  float cy = (rows - 1) * 0.5f;

  float maxr = fminf(cx, cy);
  float r = frandf(maxr * 0.35f, maxr * 2.95f);
  float a = frandf(0.0f, 2.0f * (float)M_PI);

  p->vx0 = r * cosf(a);
  p->vy0 = r * sinf(a);
  p->born = now_seconds();
  p->ch = rand_char();
}

int main(void) {
  srand((unsigned)time(NULL));

  initscr();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  timeout(0);

  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_GREEN, -1);
    // "Black hole" rainbow palette (fg on black bg)
    // If the terminal supports 256 colors, use bright extended palette indices.
    if (COLORS >= 256) {
      // blue, cyan, green, yellow, orange, red, magenta, purple, white
      short pal[] = {21, 51, 46, 226, 202, 196, 201, 93, 231};
      for (int i = 0; i < 9; i++) init_pair((short)(20 + i), pal[i], COLOR_BLACK);
    } else {
      // fallback: basic colors (still rainbow-ish)
      short pal[] = {COLOR_BLUE, COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW, COLOR_RED, COLOR_MAGENTA, COLOR_WHITE};
      for (int i = 0; i < 7; i++) init_pair((short)(10 + i), pal[i], COLOR_BLACK);
    }
  }

  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  // Particle count: tweak for density
  int N = (rows * cols) / 20;
  if (N < 200) N = 200;
  Particle *P = (Particle *)calloc((size_t)N, sizeof(Particle));
  if (!P) endwin(), exit(1);

  for (int i = 0; i < N; i++) respawn(&P[i], cols, rows);

  const float SCALE = 0.76f;     // matches your equation (· 0.76)
  const float RADIUS_MULT = 2.0f; // increase/decrease overall swirl radius
  const float X_MULT = 2.0f;       // horizontal stretch (increase for more left/right motion)
  const float Y_MULT = 1.0f;       // vertical stretch
  const float SPEED = 1.35f;     // tweak swirl speed
  const float MIN_R = 3.0f;      // respawn when near center
  const int   FPS_US = 3280;    // 200 fps

  // Black-hole palette layout (depends on terminal color support)
  const int BH_PAIR_BASE  = (has_colors() && COLORS >= 256) ? 20 : 10;
  const int BH_PAIR_COUNT = (has_colors() && COLORS >= 256) ? 9 : 7;

  int bh_mode = 0; // toggled with 'R' : black-hole-like palette using velocity + radius

  while (1) {
    int ch = getch();
    if (ch == 'q' || ch == 'Q') break;
    if (ch == 'r' || ch == 'R') bh_mode = !bh_mode;

    // Handle terminal resize
    int newr, newc;
    getmaxyx(stdscr, newr, newc);
    if (newr != rows || newc != cols) {
      rows = newr; cols = newc;
      clear();
      for (int i = 0; i < N; i++) respawn(&P[i], cols, rows);
    }

    float cx = (cols - 1) * 0.5f;
    float cy = (rows - 1) * 0.5f;
    // Max visible radius in the *un-stretched* (vx,vy) space
    float maxr_vis = fminf(cx / X_MULT, cy / Y_MULT);

    // Clear each frame (simple "cmatrix-like" refresh)
    erase();

    float tnow = now_seconds();

    for (int i = 0; i < N; i++) {
      float age = (tnow - P[i].born) * SPEED;
      float M[2][2];
      expA(age, M);

      float vx = RADIUS_MULT * SCALE * (M[0][0] * P[i].vx0 + M[0][1] * P[i].vy0);
      float vy = RADIUS_MULT * SCALE * (M[1][0] * P[i].vx0 + M[1][1] * P[i].vy0);
      float sx = X_MULT * vx;
      float sy = Y_MULT * vy;
      float r = sqrtf(vx * vx + vy * vy);
      int x = (int)lroundf(cx + sx);
      int y = (int)lroundf(cy + sy);

      // Respawn if too close to center or off-screen
      if (r < MIN_R || x < 0 || x >= cols || y < 0 || y >= rows) {
        respawn(&P[i], cols, rows);
        continue;
      }

      // Occasionally mutate character for that "matrix" vibe
      if ((rand() % 28) == 0) P[i].ch = rand_char();

      // Color/brightness
      if (has_colors()) {
        if (!bh_mode) {
          // Original "matrix green" vibe
          float a = fminf(age / 2.0f, 1.0f);
          attron(COLOR_PAIR(1));
          if (a > 0.66f) attron(A_BOLD);
          mvaddch(y, x, P[i].ch);
          if (a > 0.66f) attroff(A_BOLD);
          attroff(COLOR_PAIR(1));
        } else {
          // "Black hole" vibe: use BOTH radius and local speed (velocity) for color
          // Position in (vx,vy) space (pre-stretch)
          float shadow_r = 0.18f * maxr_vis;
          float ring_r   = 0.32f * maxr_vis;
          float ring_w   = 0.06f * maxr_vis;

          // Velocity w.r.t. real time: v_dot = SPEED * A * v
          float ax = -vx - vy; // A*[vx;vy] x-component
          float ay =  vx;      // A*[vx;vy] y-component
          float speed = SPEED * sqrtf(ax * ax + ay * ay);

          // "Swirl" ~ angular-ish speed (varies with direction, not just radius)
          float swirl = speed / (r + 1e-3f);
          float swirl_n = fminf(swirl / 2.0f, 1.0f);

          // Heat: roughly how fast it's moving relative to the max visible radius
          float heat = fminf(speed / (SPEED * (maxr_vis * 2.0f) + 1e-3f), 1.0f);

          // Rainbow index (time + radius + velocity), so it isn't just "inward gradient"
          float rr = fminf(r / (maxr_vis + 1e-3f), 1.0f);
          float hue = fmodf(0.12f * tnow + 0.85f * swirl_n + 0.40f * rr + 0.15f * heat, 1.0f);
          int rainbow_idx = (int)floorf(hue * (float)BH_PAIR_COUNT);
          if (rainbow_idx < 0) rainbow_idx = 0;
          if (rainbow_idx >= BH_PAIR_COUNT) rainbow_idx = BH_PAIR_COUNT - 1;
          int rainbow_pair = BH_PAIR_BASE + rainbow_idx;

          int pair = rainbow_pair;
          int do_bold = 0;
          int do_dim  = 0;
          int do_blink = 0;
          int draw_it = 1;

          // Deep shadow: mostly empty/dim near the center
          if (r < shadow_r) {
            if ((rand() & 3) != 0) {
              draw_it = 0; // skip most chars to make a darker "shadow"
            } else {
              pair = BH_PAIR_BASE;
              do_dim = 1;
            }
          } else {
            // Bright photon-ring-like band, thickness reacts to swirl
            float ring_thick = ring_w * (0.6f + 0.8f * swirl_n);
            if (fabsf(r - ring_r) < ring_thick) {
              // Ring flashes between white-hot and rainbow depending on swirl/time
              int white_pair = BH_PAIR_BASE + (BH_PAIR_COUNT - 1);
              pair = (((int)(tnow * 14.0f) & 1) || swirl_n > 0.55f) ? white_pair : rainbow_pair;
              do_bold = 1;
              if (swirl_n > 0.75f) do_blink = 1;
            } else {
              // Disk color is rainbow_pair; intensity comes from velocity/"heat" and swirl
              float t = 0.60f * heat + 0.40f * swirl_n;
              pair = rainbow_pair;

              // Make it "flashy": occasional sparkles for fast-moving bits
              if (t > 0.85f) {
                do_bold = 1;
                if ((rand() % 10) == 0) {
                  pair = BH_PAIR_BASE + (BH_PAIR_COUNT - 1); // white sparkle
                  do_blink = 1;
                }
              } else if (t > 0.65f) {
                do_bold = 1;
              } else if (t < 0.25f) {
                do_dim = 1;
              }

              // Rare global twinkle (keeps it lively)
              if ((rand() & 127) == 0) {
                pair = BH_PAIR_BASE + (BH_PAIR_COUNT - 1);
                do_bold = 1;
                do_blink = 1;
              }
            }
          }

          if (draw_it) {
            attron(COLOR_PAIR(pair));
            if (do_bold)  attron(A_BOLD);
            if (do_dim)   attron(A_DIM);
            if (do_blink) attron(A_BLINK);
            mvaddch(y, x, P[i].ch);
            if (do_blink) attroff(A_BLINK);
            if (do_dim)   attroff(A_DIM);
            if (do_bold)  attroff(A_BOLD);
            attroff(COLOR_PAIR(pair));
          }
        }
      } else {
        mvaddch(y, x, P[i].ch);
      }
    }

    refresh();
    usleep(FPS_US);
  }

  free(P);
  endwin();
  return 0;
}
