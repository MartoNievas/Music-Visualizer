#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#include "tinyfiledialogs.h"
#define N (1 << 13) // FFT size (8192)
#define BARS 64
#define FONT_SIZE 64
#define PI 3.14159265358979323846f

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])
typedef struct {
  Music music;
  Font font;
  bool error;
  bool has_music;
  bool paused;
  unsigned sample_rate;
} Plug;

typedef struct {
  char *file_path;
  Music music;
} Track;

typedef struct {
  Track *items;
  size_t count;
  size_t capacity;
} Tracks;

typedef enum { PAUSE_BUTTON, FULLSCREEN } UiElems;

static Plug *plug = NULL;
Tracks tracks = {0};
/* ===================== AUDIO DATA ===================== */

static float samples[N];
static float window[N];
static float complex spectrum[N];
static float bars[BARS];
static bool window_ready = false;

/* ===================== FFT ===================== */

static void fft(float in[], size_t stride, float complex out[], size_t n) {
  if (n == 1) {
    out[0] = in[0];
    return;
  }

  fft(in, stride * 2, out, n / 2);
  fft(in + stride, stride * 2, out + n / 2, n / 2);

  for (size_t k = 0; k < n / 2; k++) {
    float t = (float)k / n;
    float complex v = cexpf(-2.0f * I * PI * t) * out[k + n / 2];
    float complex e = out[k];
    out[k] = e + v;
    out[k + n / 2] = e - v;
  }
}

static float amp(float complex z) {
  float a = fabsf(crealf(z));
  float b = fabsf(cimagf(z));
  return (a > b) ? a : b;
}

/* ===================== AUDIO CALLBACK ===================== */

static void audio_callback(void *bufferData, unsigned int frames) {
  float (*fs)[plug->music.stream.channels] = bufferData;

  for (unsigned i = 0; i < frames; i++) {
    memmove(samples, samples + 1, (N - 1) * sizeof(float));
    samples[N - 1] = fs[i][0];
  }
}

/* ===================== INIT ===================== */

void plug_init(void) {
  plug = calloc(1, sizeof(*plug));
  assert(plug);

  plug->font = LoadFontEx("./fonts/Alegreya-Regular.ttf", FONT_SIZE, NULL, 0);

  memset(samples, 0, sizeof(samples));
  memset(bars, 0, sizeof(bars));
}

/* ===================== HOT RELOAD ===================== */

Plug *plug_pre_reload(void) {
  if (plug->has_music) {
    DetachAudioStreamProcessor(plug->music.stream, audio_callback);
  }
  return plug;
}

void plug_post_reload(Plug *prev) {
  plug = prev;
  if (plug->has_music) {
    AttachAudioStreamProcessor(plug->music.stream, audio_callback);
  }
}

/* ===================== UPDATE ===================== */

static void draw_progress_bar(void) {
  if (!plug->has_music)
    return;

  float played = GetMusicTimePlayed(plug->music);
  float total = GetMusicTimeLength(plug->music);
  if (total <= 0.0f)
    return;

  float t = played / total;
  if (t < 0)
    t = 0;
  if (t > 1)
    t = 1;

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  float x = t * (float)w;
  float bar_width = 6.0f;

  // BackGround
  DrawRectangle(0, h - 150, w, 200, BLACK);
  // progress bar
  DrawRectangle(x - bar_width * 0.5f, h - 150, bar_width, 200,
                (Color){100, 180, 255, 220});

  // Interactive
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 mouse_position = GetMousePosition();
    int x_current = mouse_position.x;
    int y_current = mouse_position.y;
    if ((x_current >= 0 && x_current < w) &&
        (y_current >= h - 150 && y_current < h)) {
      t = (float)x_current / w;
      SeekMusicStream(plug->music, total * t);
    }
  }
}

static void bars_render_visualizer(void) {
  int w = GetRenderWidth();
  int h = GetRenderHeight();

  BeginDrawing();
  ClearBackground((Color){0x18, 0x18, 0x18, 0xFF});

  if (plug->has_music) {
    float cw = (float)w / BARS;

    for (int i = 0; i < BARS; i++) {
      float bh = bars[i] * h * 0.9f;
      DrawRectangle(i * cw, (h - bh) - 150, cw - 2, bh, GREEN);
    }
  } else {
    const char *msg = plug->error
                          ? "Could not load file"
                          : " Select File On Click\n (Or Just Drop Here)";

    Color col = plug->error ? RED : WHITE;

    Vector2 size = MeasureTextEx(plug->font, msg, plug->font.baseSize, 0);

    DrawTextEx(plug->font, msg,
               (Vector2){(float)w / 2 - size.x / 2, (float)h / 2 - size.y / 2},
               plug->font.baseSize, 0, col);
  }

  EndDrawing();
}

static void fft_render_visualizar(void) {
  if (plug->has_music && !plug->paused) {

    if (!window_ready) {
      for (size_t i = 0; i < N; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N - 1)));
      }
      window_ready = true;
    }

    float tmp[N];
    for (size_t i = 0; i < N; i++) {
      tmp[i] = samples[i] * window[i];
    }

    fft(tmp, 1, spectrum, N);

    float max_amp = 1e-6f;
    for (size_t i = 0; i < N / 2; i++) {
      float a = amp(spectrum[i]);
      if (a > max_amp)
        max_amp = a;
    }

    float freq_min = 20.0f;
    float freq_max = plug->sample_rate * 0.5f;

    for (int i = 0; i < BARS; i++) {
      float t0 = (float)i / BARS;
      float t1 = (float)(i + 1) / BARS;

      float f0 = freq_min * powf(freq_max / freq_min, t0);
      float f1 = freq_min * powf(freq_max / freq_min, t1);

      size_t k0 = (size_t)(f0 * N / plug->sample_rate);
      size_t k1 = (size_t)(f1 * N / plug->sample_rate);
      if (k1 <= k0)
        k1 = k0 + 1;

      float a = 0.0f;
      for (size_t k = k0; k < k1 && k < N / 2; k++) {
        a += amp(spectrum[k]);
      }
      a /= (k1 - k0);

      float target = a / max_amp;
      bars[i] += 0.2f * (target - bars[i]); // smoothing
    }
  }
}

static void input_visualizer(void) {
  if (IsKeyPressed(KEY_SPACE) && plug->has_music) {
    if (plug->paused) {
      ResumeMusicStream(plug->music);
      plug->paused = false;
    } else {
      PauseMusicStream(plug->music);
      plug->paused = true;
    }
  }

  if (IsKeyPressed(KEY_Q) && plug->has_music) {
    StopMusicStream(plug->music);
    PlayMusicStream(plug->music);
    plug->paused = false;
  }
}

static void file_dropped_visualizer(void) {
  if (IsFileDropped()) {
    FilePathList files = LoadDroppedFiles();
    if (files.count > 0) {
      const char *path = files.paths[0];

      if (plug->has_music) {
        StopMusicStream(plug->music);
        DetachAudioStreamProcessor(plug->music.stream, audio_callback);
        UnloadMusicStream(plug->music);
      }

      plug->music = LoadMusicStream(path);

      if (plug->music.stream.buffer) {
        plug->error = false;
        plug->has_music = true;
        plug->paused = false;
        plug->sample_rate = plug->music.stream.sampleRate;

        AttachAudioStreamProcessor(plug->music.stream, audio_callback);
        SetMusicVolume(plug->music, 0.5f);
        PlayMusicStream(plug->music);
      } else {
        plug->error = true;
        plug->has_music = false;
      }
    }
    UnloadDroppedFiles(files);
  }
}

void plug_update(void) {
  /*Update music*/
  if (plug->has_music && !plug->paused) {
    UpdateMusicStream(plug->music);
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !plug->has_music) {
    printf("Left Button Is Pressed\n");

    int allow_multiple_selects = 0; // TODO: enable multiple selects
    char const *filter_params[] = {"*.wav", "*.ogg", "*.mp3", "*.qoa",
                                   "*.xm",  "*.mod", "*.flac"};
    char const *input_path = tinyfd_openFileDialog(
        "Path to music file", "./", ARRAY_LEN(filter_params), filter_params,
        "music file", allow_multiple_selects);
    if (input_path) {
      Music music = LoadMusicStream(input_path);
      if (IsMusicValid(music)) {

        plug->music = music;
        plug->has_music = true;
        plug->paused = false;
        plug->error = false;
        plug->sample_rate = music.stream.sampleRate;
        AttachAudioStreamProcessor(music.stream, audio_callback);
        SetMusicVolume(music, 0.5f);
        PlayMusicStream(music);
      } else {
        plug->error = true;
        plug->has_music = false;
      }
    }
  }
  /* ---------- INPUT ---------- */

  input_visualizer();

  /* ---------- FILE DROP ---------- */

  file_dropped_visualizer();
  /* FFT RENDER */

  draw_progress_bar();

  fft_render_visualizar();

  /* ---------- RENDER ---------- */
  bars_render_visualizer();
}
