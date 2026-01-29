#include "plug.h"
#include <assert.h>
#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include <string.h>
#define N 128

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

void plug_hello(void) { printf("Hello from plugin\n"); }

void plug_init(Plug *plug, const char *file_path) {

  if (!plug || !file_path) {
    fprintf(stderr, "ERROR: Null pointer in plug_init");
    return;
  }

  plug->music = LoadMusicStream(file_path);
  printf("music.frameCount = %u\n", plug->music.frameCount);
  printf("music.stream.sampleRate = %u\n", plug->music.stream.sampleRate);
  printf("music.stream.sampleSize = %u\n", plug->music.stream.sampleSize);
  printf("music.stream.channels = %u\n", plug->music.stream.channels);
  // assert(plug->music.stream.sampleSize == 16);
  // assert(plug->music.stream.channels == 2);

  SetMusicVolume(plug->music, 0.5f);
  PlayMusicStream(plug->music);
  AttachAudioStreamProcessor(plug->music.stream, callback);
}

void plug_update(Plug *plug) {
  UpdateMusicStream(plug->music);

  if (IsKeyPressed(KEY_SPACE)) {
    if (IsMusicStreamPlaying(plug->music))
      PauseMusicStream(plug->music);
    else
      ResumeMusicStream(plug->music);
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
