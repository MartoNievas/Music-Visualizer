#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 800
#define HEIGHT 600
#define N 128

#define ARRAY_LEN(xs) (sizeof(xs) / sizeof(xs[0]))

static const float pi = 3.14159265358979323846f;

float in[N];
float complex out[N];
float complex out_copy[N]; // para el render thread
float max_amp;
volatile bool fft_ready = false;

typedef struct {
  short left;
  short right;
} Frame;

/* FFT RECURSIVA (igual a la tuya, solo limpia) */
void fft(float in[], int stride, float complex out[], size_t size) {
  assert(size > 0);

  if (size == 1) {
    out[0] = in[0];
    return;
  }

  fft(in, stride * 2, out, size / 2);
  fft(in + stride, stride * 2, out + size / 2, size / 2);

  for (size_t k = 0; k < size / 2; k++) {
    float t = (float)k / size;
    float complex v = cexpf(-I * 2.0f * pi * t) * out[k + size / 2];
    float complex e = out[k];

    out[k] = e + v;
    out[k + size / 2] = e - v;
  }
}

float amp(float complex z) {
  float a = fabsf(crealf(z));
  float b = fabsf(cimagf(z));
  return (a > b) ? a : b;
}

/* CALLBACK DE AUDIO */
void callback(void *bufferData, unsigned int frames) {
  if (frames < N)
    return;

  Frame *fs = (Frame *)bufferData;

  for (size_t i = 0; i < N; i++) {
    in[i] = fs[i].left / 32768.0f; // normalización correcta
  }

  fft(in, 1, out, N);

  max_amp = 1e-6f;
  for (size_t i = 0; i < N / 2; i++) {
    float a = amp(out[i]);
    if (a > max_amp)
      max_amp = a;
  }

  memcpy(out_copy, out, sizeof(out));
  fft_ready = true;
}

char *shift_args(int *argc, char ***argv) {
  assert(*argc > 0);
  char *res = **argv;
  (*argv)++;
  (*argc)--;
  return res;
}

int main(int argc, char **argv) {
  const char *program = shift_args(&argc, &argv);

  if (argc == 0) {
    fprintf(stderr, "<Usage> %s <filepath>\n", program);
    return 1;
  }

  InitWindow(WIDTH, HEIGHT, "Music Visualizer");
  InitAudioDevice();

  Music music = LoadMusicStream(argv[0]);

  AttachAudioStreamProcessor(music.stream, callback);
  PlayMusicStream(music);
  SetMusicVolume(music, 1.0f);

  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    UpdateMusicStream(music);

    if (IsKeyPressed(KEY_SPACE)) {
      if (IsMusicStreamPlaying(music))
        PauseMusicStream(music);
      else
        ResumeMusicStream(music);
    }

    BeginDrawing();
    ClearBackground(BLACK);

    if (fft_ready) {
      int w = GetRenderWidth();
      int h = GetRenderHeight();
      float cell_width = (float)w / ((float)N / 2);

      for (size_t i = 0; i < N / 2; i++) {
        float t = amp(out_copy[i]) / max_amp;
        float bar_h = t * h * 0.9f;

        DrawRectangle((int)(i * cell_width), (float)7 * h / 8 - bar_h,
                      (int)(cell_width - 1), (int)bar_h, RED);
      }
    }

    EndDrawing();
  }

  UnloadMusicStream(music);
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
