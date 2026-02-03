#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>
#include <time.h>
#include <unistd.h>

#include "plug.h"
#include "tinyfiledialogs.h"
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"
#define DURATION_BAR 2.0f
#define N (1 << 13)
#define BARS 128
#define FONT_SIZE 64
#define PI 3.14159265358979323846f

static double master_vol = 0.5f;
static double volume_saved = 0.0f;

typedef struct {
  const char *file_name;
  Music music;
} Track;

typedef struct {
  Track *items;
  size_t count;
  size_t capacity;
} Tracks;

typedef enum {
  PLAY_UI_ICON,
  VOLUME_UI_ICON,
  FULLSCREEN_UI_ICON,
  COUNT_UI_ICONS,
} Ui_Icon;

typedef struct {
  bool visible;
  Rectangle bounds;
  float value;
} VolumeSlider;

typedef struct {
  Font font;
  Tracks tracks;
  int current_track;
  Texture2D icons_textures[COUNT_UI_ICONS];

  bool error;
  bool has_music;
  bool paused;
  bool fullscreen;
  unsigned sample_rate;

  int volume_level;

  double last_mouse_move_time;
  bool mouse_active;

} Plug;

static_assert(COUNT_UI_ICONS == 3, "Amount of icons changed");
const static char *ui_resources_icons[COUNT_UI_ICONS] = {
    [FULLSCREEN_UI_ICON] = "resources/icons/fullscreen.png",
    [PLAY_UI_ICON] = "resources/icons/play.png",
    [VOLUME_UI_ICON] = "resources/icons/volume.png",
};

static Rectangle ui_recs[COUNT_UI_ICONS];
static Plug *plug = NULL;
static VolumeSlider volume_slider = {0};

static float samples[N];
static float window[N];
static float complex spectrum[N];
static float bars[BARS];
static bool window_ready = false;

void plug_free_resource(void *data) { UnloadFileData(data); }

void *plug_load_resoruces(const char *file_path, size_t *size) {
  int data_size;
  void *data = LoadFileData(file_path, &data_size);
  *size = data_size;
  return data;
}

static Track *current_track(void) {
  if (0 <= plug->current_track &&
      (size_t)plug->current_track < plug->tracks.count) {
    return &plug->tracks.items[plug->current_track];
  }
  return NULL;
}

static void update_mouse_state(void) {
  if (!plug->fullscreen)
    return;
  const double MOUSE_TIMEOUT = 2.0f;

  Vector2 delta = GetMouseDelta();
  if (delta.x != 0 || delta.y != 0) {
    plug->last_mouse_move_time = GetTime();
  }

  plug->mouse_active = (GetTime() - plug->last_mouse_move_time) < MOUSE_TIMEOUT;
}

static void compute_fft(float in[], size_t stride, float complex out[],
                        size_t n) {
  if (n == 1) {
    out[0] = in[0];
    return;
  }

  compute_fft(in, stride * 2, out, n / 2);
  compute_fft(in + stride, stride * 2, out + n / 2, n / 2);

  for (size_t k = 0; k < n / 2; k++) {
    float t = (float)k / n;
    float complex v = cexpf(-2.0f * I * PI * t) * out[k + n / 2];
    float complex e = out[k];
    out[k] = e + v;
    out[k + n / 2] = e - v;
  }
}

static float get_amplitude(float complex z) {
  float a = fabsf(crealf(z));
  float b = fabsf(cimagf(z));
  return (a > b) ? a : b;
}

static void process_audio(void *bufferData, unsigned int frames) {
  float (*fs)[current_track()->music.stream.channels] = bufferData;

  for (unsigned i = 0; i < frames; i++) {
    memmove(samples, samples + 1, (N - 1) * sizeof(float));
    samples[N - 1] = fs[i][0];
  }
}

static void draw_progress(void) {
  if (!plug->has_music || plug->fullscreen)
    return;

  float played = GetMusicTimePlayed(current_track()->music);
  float total = GetMusicTimeLength(current_track()->music);
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

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();

    bool clicking_ui_button = false;
    for (int i = 0; i < COUNT_UI_ICONS; i++) {
      if (CheckCollisionPointRec(m, ui_recs[i])) {
        clicking_ui_button = true;
        break;
      }
    }

    if (!clicking_ui_button && m.y >= h - 150 && m.y < h) {
      float nt = m.x / (float)w;
      if (nt < 0)
        nt = 0;
      if (nt > 1)
        nt = 1;
      SeekMusicStream(current_track()->music, total * nt);
    }
  }
}

static void draw_queue(void) {
  if (plug->fullscreen || !plug->has_music)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  float queue_width = w * 0.20f;
  float queue_height = h - 150;

  DrawRectangle(0, 0, queue_width, queue_height,
                (Color){0x10, 0x10, 0x10, 0xFF});
}

static void draw_bars(void) {
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
        DrawRectangle(i * cw + 0.20f * w, (h - bh) - 150 - h * 0.05f, cw - 2,
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

static void update_visualizer(void) {
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

    compute_fft(tmp, 1, spectrum, N);

    float max_amp = 1e-6f;
    for (size_t i = 0; i < N / 2; i++) {
      float a = get_amplitude(spectrum[i]);
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
        a += get_amplitude(spectrum[k]);
      }
      a /= (k1 - k0);

      float target = a / max_amp;
      bars[i] += 0.2f * (target - bars[i]);
    }
  }
}

static void draw_volume_slider(void) {
  if (!volume_slider.visible)
    return;

  Rectangle slider = volume_slider.bounds;

  DrawRectangleRec(slider, (Color){0x20, 0x20, 0x20, 0xF0});
  DrawRectangleLinesEx(slider, 1, (Color){0x50, 0x50, 0x50, 0xFF});

  float fill_width = slider.width * volume_slider.value;
  Rectangle fill = {slider.x, slider.y, fill_width, slider.height};

  DrawRectangleRec(fill, (Color){100, 180, 255, 220});

  float indicator_x = slider.x + fill_width;
  float indicator_y = slider.y + slider.height * 0.5f;
  DrawCircle(indicator_x, indicator_y, 6, WHITE);
  DrawCircle(indicator_x, indicator_y, 4, (Color){100, 180, 255, 255});

  char percent_text[16];
  snprintf(percent_text, sizeof(percent_text), "%d%%",
           (int)(volume_slider.value * 100));
  DrawText(percent_text, slider.x + slider.width + 10,
           slider.y + (slider.height - 10) * 0.5f, 10, WHITE);

  Vector2 mouse = GetMousePosition();

  if (CheckCollisionPointRec(mouse, slider)) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      float relative_x = mouse.x - slider.x;
      float new_value = relative_x / slider.width;

      if (new_value < 0.0f)
        new_value = 0.0f;
      if (new_value > 1.0f)
        new_value = 1.0f;

      volume_slider.value = new_value;
      master_vol = new_value;
      SetMusicVolume(current_track()->music, master_vol);

      // Actualizar nivel de volumen basado en el nuevo valor
      if (master_vol <= 0.05f)
        plug->volume_level = 0;
      else if (master_vol <= 0.65f)
        plug->volume_level = 1;
      else
        plug->volume_level = 2;
    }
  }
}

static void draw_ui_bar(void) {
  if (!plug->has_music)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  Rectangle bar;
  if (plug->fullscreen) {
    if (!plug->mouse_active)
      return;

    bar = (Rectangle){
        0,
        h - h * 0.05f,
        w,
        h * 0.05f,
    };
  } else {
    bar = (Rectangle){
        w * 0.20f,
        h - 150 - h * 0.05f,
        w - w * 0.20f,
        h * 0.05f,
    };
  }

  DrawRectangleRec(bar, (Color){0x10, 0x10, 0x10, 0xFF});

  float padding = bar.height * 0.25f;
  float icon_size = bar.height - padding * 2;
  float y = bar.y + (bar.height - icon_size) * 0.5f;

  float x_left = bar.x + padding;

  {
    Texture2D tex = plug->icons_textures[PLAY_UI_ICON];
    float frame = plug->paused ? 0 : 1;
    float s = tex.height;

    Rectangle dst = {x_left, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};

    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    ui_recs[PLAY_UI_ICON] = dst;

    x_left += icon_size + padding;
  }

  {
    Texture2D tex;
    tex = plug->icons_textures[VOLUME_UI_ICON];
    float frame = plug->volume_level;
    float s = tex.height;

    Rectangle dst = {x_left, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};

    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    ui_recs[VOLUME_UI_ICON] = dst;

    Vector2 mouse = GetMousePosition();

    float slider_width = icon_size * 5.0f;
    float slider_height = icon_size * 0.5f;

    Rectangle slider_bounds =
        (Rectangle){dst.x + dst.width + padding,
                    dst.y + (dst.height - slider_height) * 0.5f, slider_width,
                    slider_height};

    if (CheckCollisionPointRec(mouse, dst) ||
        CheckCollisionPointRec(mouse, slider_bounds)) {
      volume_slider.visible = true;
      volume_slider.bounds = slider_bounds;
      volume_slider.value = master_vol;
    } else {
      volume_slider.visible = false;
    }

    x_left += icon_size + padding;
  }

  {
    Texture2D tex = plug->icons_textures[FULLSCREEN_UI_ICON];
    float frame = plug->fullscreen ? 1 : 0;
    float s = tex.height;

    float x_right = bar.x + bar.width - padding - icon_size;

    Rectangle dst = {x_right, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};

    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    ui_recs[FULLSCREEN_UI_ICON] = dst;
  }
}

static void handle_input(void) {
  if (IsKeyPressed(KEY_F)) {
    plug->fullscreen = !plug->fullscreen;
  }

  if (plug->has_music) {
    if (IsKeyPressed(KEY_SPACE)) {
      if (plug->paused) {
        ResumeMusicStream(current_track()->music);
        plug->paused = false;
      } else {
        PauseMusicStream(current_track()->music);
        plug->paused = true;
      }
    }

    Vector2 mouse = GetMousePosition();
    if (IsKeyPressed(KEY_M) ||
        (CheckCollisionPointRec(mouse, ui_recs[VOLUME_UI_ICON]) &&
         IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
      if (master_vol != 0.0f) {
        volume_saved = master_vol;
        master_vol = 0.0f;
        plug->volume_level = 0;
      } else {
        master_vol = volume_saved;
        if (master_vol <= 0.05f)
          plug->volume_level = 0;
        else if (master_vol <= 0.65f)
          plug->volume_level = 1;
        else
          plug->volume_level = 2;
      }
      SetMusicVolume(current_track()->music, master_vol);
    }

    if (IsKeyPressed(KEY_N)) {
      if (plug->tracks.count > 0) {
        StopMusicStream(current_track()->music);
        DetachAudioStreamProcessor(current_track()->music.stream,
                                   process_audio);

        if ((size_t)plug->current_track + 1 < plug->tracks.count) {
          plug->current_track++;
        } else {
          plug->current_track = 0;
        }

        Music new_music = current_track()->music;
        plug->sample_rate = new_music.stream.sampleRate;
        AttachAudioStreamProcessor(new_music.stream, process_audio);
        SetMusicVolume(new_music, master_vol);
        PlayMusicStream(new_music);
        plug->paused = false;
      }
    }

    if (IsKeyPressed(KEY_P)) {
      if (plug->tracks.count > 0) {
        StopMusicStream(current_track()->music);
        DetachAudioStreamProcessor(current_track()->music.stream,
                                   process_audio);

        if (plug->current_track > 0) {
          plug->current_track--;
        } else {
          plug->current_track = plug->tracks.count - 1;
        }

        Music new_music = current_track()->music;
        plug->sample_rate = new_music.stream.sampleRate;
        AttachAudioStreamProcessor(new_music.stream, process_audio);
        SetMusicVolume(new_music, master_vol);
        PlayMusicStream(new_music);
        plug->paused = false;
      }
    }
  }
}

static void handle_file_drop(void) {
  if (IsFileDropped()) {

    FilePathList files = LoadDroppedFiles();

    for (size_t i = 0; i < files.count; i++) {
      Music music = LoadMusicStream(files.paths[i]);

      if (IsMusicValid(music)) {
        AttachAudioStreamProcessor(music.stream, process_audio);
        if (plug->has_music) {
          char *file_path = strdup(files.paths[i]);
          assert(file_path != NULL);
          da_append(&plug->tracks, (CLITERAL(Track){
                                       .file_name = file_path,
                                       .music = music,
                                   }));
        } else {
          char *file_path = strdup(files.paths[i]);
          if (file_path != NULL) {
            da_append(&plug->tracks, (CLITERAL(Track){
                                         .file_name = file_path,
                                         .music = music,
                                     }));
          }
          PlayMusicStream(current_track()->music);
          ;
          plug->has_music = true;
        }
      }
    }

    UnloadDroppedFiles(files);
    if (current_track() == NULL && plug->tracks.count > 0) {
      plug->current_track = 0;
      PlayMusicStream(plug->tracks.items[0].music);
    }
  }
}

static void load_assets(void) {
  size_t data_size = 0;
  void *data = NULL;

  const char *alegreya_path = "resources/fonts/Alegreya-Regular.ttf";
  data = plug_load_resoruces(alegreya_path, &data_size);
  plug->font = LoadFontFromMemory(GetFileExtension(alegreya_path), data,
                                  data_size, FONT_SIZE, NULL, 0);

  plug_free_resource(data);

  for (Ui_Icon i = 0; i < COUNT_UI_ICONS; i++) {
    data = plug_load_resoruces(ui_resources_icons[i], &data_size);
    Image image = LoadImageFromMemory(GetFileExtension(ui_resources_icons[i]),
                                      data, data_size);

    plug->icons_textures[i] = LoadTextureFromImage(image);
    GenTextureMipmaps(&plug->icons_textures[i]);
    SetTextureFilter(plug->icons_textures[i], TEXTURE_FILTER_BILINEAR);
    UnloadImage(image);
    plug_free_resource(data);
  }
}

static void unload_assets(void) {
  UnloadFont(plug->font);
  for (Ui_Icon icon = 0; icon < COUNT_UI_ICONS; icon++) {
    UnloadTexture(plug->icons_textures[icon]);
  }
}

void plug_post_reload(Plug *prev) {
  plug = prev;
  if (plug->has_music) {
    AttachAudioStreamProcessor(current_track()->music.stream, process_audio);
  }
  load_assets();
}

Plug *plug_pre_reload(void) {
  if (plug->has_music) {
    DetachAudioStreamProcessor(current_track()->music.stream, process_audio);
  }
  unload_assets();

  return plug;
}

static bool is_ui_bar_active(void) {
  if (!plug->has_music)
    return false;

  if (plug->fullscreen)
    return plug->mouse_active;

  return true;
}

void plug_init(void) {
  plug = calloc(1, sizeof(*plug));
  assert(plug);

  memset(plug, 0, sizeof(*plug));

  load_assets();
  plug->fullscreen = false;
  plug->mouse_active = false;
  plug->last_mouse_move_time = -100.0f;
  plug->volume_level = 1;

  memset(samples, 0, sizeof(samples));
  memset(bars, 0, sizeof(bars));
  memset(&volume_slider, 0, sizeof(volume_slider));

  SetMasterVolume(master_vol);
  SetTargetFPS(60);
}

void plug_update(void) {
  if (plug->has_music && !plug->paused) {
    UpdateMusicStream(current_track()->music);
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !plug->has_music) {
    char const *filters[] = {"*.wav", "*.ogg", "*.mp3", "*.flac"};
    char const *path = tinyfd_openFileDialog(
        "Select music", "./", ARRAY_LEN(filters), filters, "music", 0);
    if (path) {
      Music music = LoadMusicStream(path);
      if (IsMusicValid(music)) {
        da_append(&plug->tracks, (CLITERAL(Track){
                                     .music = music,
                                     .file_name = path,
                                 }));
        plug->has_music = true;
        plug->paused = false;
        plug->error = false;
        plug->sample_rate = music.stream.sampleRate;
        AttachAudioStreamProcessor(music.stream, process_audio);
        SetMusicVolume(music, master_vol);
        PlayMusicStream(music);
      } else {
        plug->error = true;
        plug->has_music = false;
      }
    }
  }

  update_mouse_state();
  handle_input();
  handle_file_drop();

  BeginDrawing();
  ClearBackground((Color){0x18, 0x18, 0x18, 0xFF});

  update_visualizer();
  draw_queue();
  draw_progress();
  draw_ui_bar();
  draw_volume_slider();

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && is_ui_bar_active()) {
    Vector2 m = GetMousePosition();

    if (CheckCollisionPointRec(m, ui_recs[PLAY_UI_ICON])) {
      plug->paused = !plug->paused;

      if (plug->paused)
        PauseMusicStream(current_track()->music);
      else
        ResumeMusicStream(current_track()->music);
    }

    else if (CheckCollisionPointRec(m, ui_recs[FULLSCREEN_UI_ICON])) {
      plug->fullscreen = !plug->fullscreen;
    }
  }

  for (size_t i = 0; i < plug->tracks.count; i++) {
    const char *file = plug->tracks.items[i].file_name;
    printf("file path: %s \n", file);
  }

  draw_bars();
  EndDrawing();
}
