#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 800
#define HEIGHT 600
#define N 256
float in[N];
float complex out[N];
float max_amp;

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])

float pi;
typedef struct {
  float left;
  float rigth;
} Frame;

void fft(float in[], int stride, float complex out[], size_t size) {

  assert(size > 0);

  if (size == 1) {
    out[0] = in[0 * stride];
    return;
  }
  fft(in, stride * 2, out, size / 2);
  fft(in + stride, stride * 2, out + size / 2, size / 2);

  for (size_t k = 0; k < size / 2; k++) {
    float t = (float)k / size;
    float complex v = cexp(-I * 2 * pi * t) * out[k + size / 2];
    float complex e = out[k];

    out[k] = e + v;
    out[k + size / 2] = e - v;
  }
}

float amp(float complex z) {
  float a = fabsf(crealf(z));
  float b = fabsf(cimagf(z));
  if (a < b)
    return b;
  return a;
}

void callback(void *bufferData, unsigned int frames) {
  if (frames < N)
    return;

  Frame *fs = bufferData;

  for (size_t i = 0; i < N; ++i) {
    in[i] = fs[i].left;
  }

  fft(in, 1, out, N);

  max_amp = 0.0f;
  for (size_t i = 0; i < N; ++i) {
    float a = amp(out[i]);
    if (max_amp < a)
      max_amp = a;
  }
}

char *shift_args(int *argc, char ***argv) {
  assert(*argc > 0);

  char *res = (**argv);
  (*argv) += 1;
  (*argc) -= 1;
  return res;
}

int main(int argc, char **argv) {

  const char *program = shift_args(&argc, &argv);

  if (argc == 0) {
    fprintf(stderr, "<Usage> %s <filepath>\n", program);
    fprintf(stderr, "ERROR: no input file is provided\n");
    return 1;
  }
  printf("Hello World");

  InitWindow(WIDTH, HEIGHT, "Music");
  InitAudioDevice();

  char *filepath = (*argv);
  Music music = LoadMusicStream(filepath);
  printf("frameCount = %u\n", music.frameCount);
  printf("sampleRate = %u\n", music.stream.sampleRate);
  printf("sampleSize = %u\n", music.stream.sampleSize);
  printf("channels = %u\n", music.stream.channels);
  // assert(music.stream.sampleSize == 16);
  // assert(music.stream.channels == 2);
  SetTargetFPS(60);

  PlayMusicStream(music);
  SetMusicVolume(music, 1.0f);
  AttachAudioStreamProcessor(music.stream, callback);
  while (!WindowShouldClose()) {
    UpdateMusicStream(music);

    if (IsKeyPressed(KEY_SPACE)) {
      if (!IsMusicStreamPlaying(music)) {
        ResumeMusicStream(music);
      } else {
        PauseMusicStream(music);
      }
    }

    if (IsFileDropped()) {
    }

    int w = GetRenderWidth();
    int h = GetRenderHeight();

    // Reproduce song
    BeginDrawing();
    ClearBackground(BLACK);
    float cell_width = (float)w / N;
    for (size_t i = 0; i < N; i++) {
      float t = amp(out[i]) / max_amp;
      DrawRectangle(i * cell_width, (float)h / 2 - (float)h / 2 * t, cell_width,
                    (float)h / 2 * t, RED);
    }
    EndDrawing();
  }
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
