#include "plug.h"
#include <assert.h>
#include <math.h>
#include <raylib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define N 256
static const float pi;

float in[N];
float complex out[N];
float max_amp;

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
  if (a < b)
    return b;
  return a;
}

void callback(void *bufferData, unsigned int frames) {
  Frame *fs = bufferData;

  for (size_t i = 0; i < frames; ++i) {
    memmove(in, in + 1, (N - 1) * sizeof(in[0]));
    in[N - 1] = fs[i].left;
  }
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
    if (IsMusicStreamPlaying(plug->music)) {
      PauseMusicStream(plug->music);
    } else {
      ResumeMusicStream(plug->music);
    }
  }

  if (IsKeyPressed(KEY_Q)) {
    StopMusicStream(plug->music);
    PlayMusicStream(plug->music);
  }

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  BeginDrawing();
  ClearBackground(CLITERAL(Color){0x18, 0x18, 0x18, 0xFF});

  fft(in, 1, out, N);

  float max_amp = 0.0f;
  for (size_t i = 0; i < N; ++i) {
    float a = amp(out[i]);
    if (max_amp < a)
      max_amp = a;
  }

  float step = 1.06;
  size_t m = 0;
  for (float f = 20.0f; (size_t)f < N; f *= step) {
    m += 1;
  }

  float cell_width = (float)w / m;
  m = 0;
  for (float f = 20.0f; (size_t)f < N; f *= step) {
    float f1 = f * step;
    float a = 0.0f;
    for (size_t q = (size_t)f; q < N && q < (size_t)f1; ++q) {
      a += amp(out[q]);
    }
    a /= (size_t)f1 - (size_t)f + 1;
    float t = a / max_amp;
    DrawRectangle(m * cell_width, h / 2 - h / 2 * t, cell_width, h / 2 * t,
                  GREEN);
    // DrawCircle(m*cell_width, h/2, h/2*t, GREEN);
    m += 1;
  }
  EndDrawing();
}

void plug_world(void) { printf("Foo Bar\n"); }

void plug_pre_reload(Plug *plug) {
  DetachAudioStreamProcessor(plug->music.stream, callback);
}

void plug_post_reload(Plug *plug) {
  AttachAudioStreamProcessor(plug->music.stream, callback);
}
