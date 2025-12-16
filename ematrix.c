// swirlmatrix.c
// Build: gcc -O2 -Wall -Wextra ematrix.c -lncurses -lm -o ematrix
// Run:   ./swirlmatrix
//
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
  float r = frandf(maxr * 0.35f, maxr * 0.95f);
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
  }

  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  // Particle count: tweak for density
  int N = (rows * cols) / 12;
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
  const int   FPS_US = 8250;    // 120 fps

  while (1) {
    int ch = getch();
    if (ch == 'q' || ch == 'Q') break;

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

      // Brightness based on age
      float a = fminf(age / 2.0f, 1.0f);
      if (has_colors()) {
        attron(COLOR_PAIR(1));
        if (a > 0.66f) attron(A_BOLD);
        mvaddch(y, x, P[i].ch);
        if (a > 0.66f) attroff(A_BOLD);
        attroff(COLOR_PAIR(1));
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
