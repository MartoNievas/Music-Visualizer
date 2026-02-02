
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#include "plug.h"
#include "tinyfiledialogs.h"
#define DURATION_BAR 2.0f // 2 seconds
#define N (1 << 13)
#define BARS 64
#define FONT_SIZE 64
#define PI 3.14159265358979323846f

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])

typedef struct {
  const char *file_name;
  const Music music;
} Track;

typedef struct {
  Track *items;
  size_t count;
  size_t capacity;
} Tracks;

typedef enum {
  PLAY_UI_ICON,
  FULLSCREEN_UI_ICON,
  VOLUME_UI_ICON,
  COUNT_UI_ICONS,
} Ui_Icon;

typedef struct {

  // Resoruces
  Music music;
  Font font;
  Tracks tracks;
  Track current;
  Texture2D icons_textures[COUNT_UI_ICONS];
  // Control
  bool error;
  bool has_music;
  bool paused;
  bool fullscreen;
  unsigned sample_rate;

  // Mouse state
  double last_mouse_move_time;
  bool mouse_active;

} Plug;

static_assert(COUNT_UI_ICONS == 3, "Amount of icons changed");

static Plug *plug = NULL;

static float samples[N];
static float window[N];
static float complex spectrum[N];
static float bars[BARS];
static bool window_ready = false;

static void update_mouse_activity(void) {
  if (!plug->fullscreen)
    return;
  const double MOUSE_TIMEOUT = 2.0f;

  Vector2 delta = GetMouseDelta();
  if (delta.x != 0 || delta.y != 0) {
    plug->last_mouse_move_time = GetTime();
  }

  plug->mouse_active = (GetTime() - plug->last_mouse_move_time) < MOUSE_TIMEOUT;
}

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

static void audio_callback(void *bufferData, unsigned int frames) {
  float (*fs)[plug->music.stream.channels] = bufferData;

  for (unsigned i = 0; i < frames; i++) {
    memmove(samples, samples + 1, (N - 1) * sizeof(float));
    samples[N - 1] = fs[i][0];
  }
}

void plug_init(void) {
  plug = calloc(1, sizeof(*plug));
  assert(plug);

  plug->font =
      LoadFontEx("./resources/fonts/Alegreya-Regular.ttf", FONT_SIZE, NULL, 0);

  plug->fullscreen = false;
  plug->mouse_active = false;
  plug->last_mouse_move_time = -100.0f;
  memset(samples, 0, sizeof(samples));
  memset(bars, 0, sizeof(bars));
}

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

static void draw_progress_bar(void) {
  if (!plug->has_music || plug->fullscreen)
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

  float x = t * w;
  float bar_width = 6.0f;

  DrawRectangle(0, h - 150, w, 200, BLACK);
  DrawRectangle(x - bar_width * 0.5f, h - 150, bar_width, 200,
                (Color){100, 180, 255, 220});

  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();
    if (m.y >= h - 150 && m.y < h) {
      float nt = m.x / (float)w;
      if (nt < 0)
        nt = 0;
      if (nt > 1)
        nt = 1;
      SeekMusicStream(plug->music, total * nt);
    }
  }
}

static void draw_tracks_queue(void) {
  if (plug->fullscreen || !plug->has_music)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  float queue_width = w * 0.23f;
  float queue_height = h - 150;

  DrawRectangle(0, 0, queue_width, queue_height,
                (Color){0x10, 0x10, 0x10, 0xFF});
}

static void bars_render_visualizer(void) {
  int w = GetRenderWidth();
  int h = GetRenderHeight();

  if (plug->has_music) {
    float cw = (float)w / BARS;

    for (int i = 0; i < BARS; i++) {

      float bh = plug->fullscreen ? bars[i] * h * 0.9f : bars[i] * h * 0.75f;
      if (plug->fullscreen) {
        float y = plug->mouse_active ? h - bh - h * 0.05f : h - bh;

        DrawRectangle(i * cw, y, cw - 2, bh, GREEN);
      } else {
        DrawRectangle(i * cw + 0.23f * w, (h - bh) - 150 - h * 0.05f, cw - 2,
                      bh, GREEN);
      }
    }
  } else {
    const char *msg = plug->error
                          ? "Could not load file"
                          : " Select File On Click\n (Or Just Drop Here)";

    Color col = plug->error ? RED : WHITE;
    Vector2 size = MeasureTextEx(plug->font, msg, plug->font.baseSize, 0);

    DrawTextEx(plug->font, msg,
               (Vector2){w / 2.0f - size.x / 2, h / 2.0f - size.y / 2},
               plug->font.baseSize, 0, col);
  }
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
      bars[i] += 0.2f * (target - bars[i]);
    }
  }
}

static void draw_ui_icons_bar(void) {
  if (!plug->has_music)
    return;
  int w = GetRenderWidth();
  int h = GetRenderHeight();

  Rectangle bar_background;
  if (plug->fullscreen) {
    /*If dont move mouse dont draw ui icons bar*/

    if (!plug->mouse_active)
      return;

    bar_background = (Rectangle){
        .height = h * 0.05f,
        .width = w,
        .x = 0,
        .y = h - h * 0.05f,
    };

  } else {
    bar_background = (Rectangle){
        .x = w * 0.23f,
        .y = h - 150 - h * 0.05f,
        .width = w - w * 0.23f,
        .height = h * 0.05f,
    };
  }

  DrawRectangleRec(bar_background, (Color){0x10, 0x10, 0x10, 0xFF});
}

static void input_visualizer(void) {
  if (IsKeyPressed(KEY_F)) {
    plug->fullscreen = !plug->fullscreen;
  }

  if (IsKeyPressed(KEY_SPACE) && plug->has_music) {
    if (plug->paused) {
      ResumeMusicStream(plug->music);
      plug->paused = false;
    } else {
      PauseMusicStream(plug->music);
      plug->paused = true;
    }
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
  if (plug->has_music && !plug->paused) {
    UpdateMusicStream(plug->music);
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !plug->has_music) {
    char const *filters[] = {"*.wav", "*.ogg", "*.mp3", "*.flac"};
    char const *path = tinyfd_openFileDialog(
        "Select music", "./", ARRAY_LEN(filters), filters, "music", 0);
    if (path) {
      Music music = LoadMusicStream(path);
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

  update_mouse_activity();
  input_visualizer();
  file_dropped_visualizer();

  BeginDrawing();
  ClearBackground((Color){0x18, 0x18, 0x18, 0xFF});

  fft_render_visualizar();
  draw_tracks_queue();
  draw_progress_bar();
  draw_ui_icons_bar();
  bars_render_visualizer();
  EndDrawing();
}
