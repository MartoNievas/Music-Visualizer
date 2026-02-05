/**
 * @file plug.c
 * @brief Music visualizer plugin with real-time FFT analysis and playback
 * controls
 *
 * This plugin provides a real-time music visualization system using FFT
 * analysis to generate frequency-based bar graphs. It supports multiple audio
 * formats, playlist management, and interactive UI controls.
 */
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tinyfiledialogs.h"
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "../thirdparty/nob.h"

/* Configuration constants */
#define DURATION_BAR 2.0f          ///< Duration for bar animation transitions
#define N (1 << 13)                ///< FFT sample size (8192 samples)
#define BARS 72                    ///< Number of frequency bars to display
#define FONT_SIZE 64               ///< Base font size for UI text
#define PI 3.14159265358979323846f ///< Pi constant for FFT calculations

#define GLSL_VERSION 330
/* Global audio settings */

/**
 * @struct Track
 * @brief Represents a single audio track with its file path and music stream
 */
typedef struct {
  const char *file_name; ///< Path to the audio file
  Music music;           ///< Raylib music stream handle
} Track;

/**
 * @struct Tracks
 * @brief Dynamic array of tracks (playlist)
 */
typedef struct {
  Track *items;    ///< Array of track items
  size_t count;    ///< Current number of tracks
  size_t capacity; ///< Allocated capacity
} Tracks;

/**
 * @enum Ui_Icon
 * @brief Enumeration of UI icon types
 */
typedef enum {
  PLAY_UI_ICON,       ///< Play/pause button icon
  FILE_UI_ICON,       ///< Finde files in directories
  VOLUME_UI_ICON,     ///< Volume control icon
  FULLSCREEN_UI_ICON, ///< Fullscreen toggle icon
  COUNT_UI_ICONS,     ///< Total number of icon types
} Ui_Icon;

/**
 * @struct VolumeSlider
 * @brief Interactive volume slider widget state
 */
typedef struct {
  bool visible;     ///< Whether the slider is currently visible
  Rectangle bounds; ///< Slider's screen rectangle
  float value;      ///< Current slider value (0.0 to 1.0)
} VolumeSlider;

/**
 * @struct Plug
 * @brief Main plugin state containing all application data
 */
typedef struct {
  Font font;         ///< Font for UI text rendering
  Tracks tracks;     ///< Playlist of audio tracks
  int current_track; ///< Index of currently playing track
  Texture2D icons_textures[COUNT_UI_ICONS]; ///< Loaded UI icon textures

  // Shaders
  Shader circle;
  float circle_radius_location;
  float circle_power_location;

  bool error;      ///< Error state flag
  bool has_music;  ///< Whether any music is loaded
  bool paused;     ///< Playback pause state
  bool fullscreen; ///< Fullscreen visualization mode

  double last_mouse_move_time; ///< Timestamp of last mouse movement
  bool mouse_active;           ///< Whether mouse is recently active

  float queue_scroll; ///< Queue panel scroll offset

  // Volume config
  VolumeSlider volume_slider; ///< Volume slider state
  double master_vol;          ///< Master volume level (0.0 to 1.0)
  double volume_saved;        ///< Saved volume for mute/unmute toggle
  int volume_level; ///< Volume level indicator (0-2: muted, low, high)

  // Browser config
  bool show_browser;      ///< Flag para mostrar/ocultar el explorador interno
  FilePathList dir_files; ///< Lista de archivos en el directorio actual
  char current_dir[512];
  int browser_scroll;

  Rectangle ui_recs[COUNT_UI_ICONS]; ///< Collision rectangles for UI buttons
                                     ///< Global plugin instance

  /* Audio processing buffers */
  unsigned sample_rate; ///< Current track sample rate
  float samples[N];
  atomic_uint sample_write;
  float window[N];           ///< Hann window for FFT
  float complex spectrum[N]; ///< FFT output spectrum
  float smear[BARS];         ///< Smear effect buffer for motion blur
  float bars[BARS];          ///< Smoothed bar heights for visualization
  bool window_ready;         ///< Whether Hann window is initialized

  float bass_history;  ///< Persistent low-frequency energy state
  float overall_level; ///< Persistent overall volume level for dynamic scaling
} Plug;
Plug *plug = NULL;
/* Compile-time assertion to ensure icon array matches enum */
static_assert(COUNT_UI_ICONS == 4, "Amount of icons changed");

/**
 * @brief File paths for UI icon resources
 */
const static char *ui_resources_icons[COUNT_UI_ICONS] = {
    [FULLSCREEN_UI_ICON] = "resources/icons/fullscreen.png",
    [PLAY_UI_ICON] = "resources/icons/play.png",
    [VOLUME_UI_ICON] = "resources/icons/volume.png",
    [FILE_UI_ICON] = "resources/icons/file.jpg",
};

/* Global state variables */

/**
 * @brief Frees resources loaded by plug_load_resources
 * @param data Pointer to data allocated by LoadFileData
 */
void plug_free_resource(void *data) { UnloadFileData(data); }

/**
 * @brief Loads file data from disk into memory
 * @param file_path Path to the file to load
 * @param size Output parameter for file size in bytes
 * @return Pointer to allocated file data, or NULL on failure
 */
void *plug_load_resoruces(const char *file_path, size_t *size) {
  int data_size;
  void *data = LoadFileData(file_path, &data_size);
  *size = data_size;
  return data;
}

/**
 * @brief Gets pointer to the currently playing track
 * @return Pointer to current Track, or NULL if no track is playing
 */
static Track *current_track(void) {
  if (0 <= plug->current_track &&
      (size_t)plug->current_track < plug->tracks.count) {
    return &plug->tracks.items[plug->current_track];
  }
  return NULL;
}

/**
 * @brief Updates mouse activity state for UI auto-hiding in fullscreen
 *
 * Tracks mouse movement to determine if UI should be visible in fullscreen
 * mode. UI hides after MOUSE_TIMEOUT seconds of inactivity.
 */
static void update_mouse_state(void) {
  if (!plug->fullscreen)
    return;

  const double MOUSE_TIMEOUT = 2.0f; ///< Seconds of inactivity before hiding UI

  Vector2 delta = GetMouseDelta();
  if (delta.x != 0 || delta.y != 0) {
    plug->last_mouse_move_time = GetTime();
  }

  plug->mouse_active = (GetTime() - plug->last_mouse_move_time) < MOUSE_TIMEOUT;
}

/**
 * @brief Computes Fast Fourier Transform using Cooley-Tukey algorithm
 *
 * Recursive radix-2 decimation-in-time FFT implementation.
 *
 * @param in Input array of real samples
 * @param stride Stride between samples (used for recursion)
 * @param out Output array for complex frequency spectrum
 * @param n Number of samples (must be power of 2)
 */
static void compute_fft(float in[], size_t stride, float complex out[],
                        size_t n) {
  /* Base case: single sample */
  if (n == 1) {
    out[0] = in[0];
    return;
  }

  /* Recursively compute FFT of even and odd samples */
  compute_fft(in, stride * 2, out, n / 2);
  compute_fft(in + stride, stride * 2, out + n / 2, n / 2);

  /* Combine results using butterfly operations */
  for (size_t k = 0; k < n / 2; k++) {
    float t = (float)k / n;
    float complex v = cexpf(-2.0f * I * PI * t) * out[k + n / 2];
    float complex e = out[k];
    out[k] = e + v;
    out[k + n / 2] = e - v;
  }
}

/**
 * @brief Calculates amplitude of a complex number using infinity norm
 *
 * Uses max(|real|, |imag|) as a fast approximation of magnitude.
 *
 * @param z Complex number
 * @return Amplitude (infinity norm)
 */
static float get_amplitude(float complex z) {
  float a = fabsf(crealf(z));
  float b = fabsf(cimagf(z));
  return (a > b) ? a : b;
}

/**
 * @brief Audio stream callback that captures samples for visualization
 *
 * Called by Raylib for each audio buffer. Maintains a ring buffer of samples
 * for FFT processing.
 *
 * @param bufferData Interleaved audio samples from stream
 * @param frames Number of frames in buffer
 */

static void process_audio(void *bufferData, unsigned int frames) {

  if (!plug)
    return;

  Track *t = current_track();
  if (!t)
    return;

  float *fs = (float *)bufferData;
  unsigned ch = t->music.stream.channels;

  unsigned w = atomic_load_explicit(&plug->sample_write, memory_order_relaxed);

  for (unsigned i = 0; i < frames; i++) {
    plug->samples[w] = fs[i * ch]; // canal 0 (mono)
    w = (w + 1) % N;
  }

  atomic_store_explicit(&plug->sample_write, w, memory_order_release);
}

/**
 * @brief Draws playback progress bar and handles seeking
 *
 * Displays current position in track and allows clicking to seek.
 * Only shown in windowed mode.
 */
static void draw_progress(void) {
  if (!plug->has_music || plug->fullscreen)
    return;

  /* Calculate progress percentage */
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

  /* Draw progress indicator */
  float x = t * w;
  float bar_width = 10.0f;

  DrawRectangle(0, h - 150, w, 200, BLACK);
  DrawRectangle(x - bar_width * 0.5f, h - 150, bar_width, 200, BLUE);

  /* Handle click-to-seek */
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();

    /* Check if clicking on UI buttons (to avoid conflict) */
    bool clicking_ui_button = false;
    for (int i = 0; i < COUNT_UI_ICONS; i++) {
      if (CheckCollisionPointRec(m, plug->ui_recs[i])) {
        clicking_ui_button = true;
        break;
      }
    }

    /* Perform seek if clicking on progress bar */
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

/**
 * @brief Switches to a different track in the playlist
 *
 * Stops current track, updates processors, and starts new track.
 * Handles wraparound for next/previous navigation.
 *
 * @param index Index of track to switch to (-1 for last, >count wraps to first)
 */
static void switch_track(int index) {
  if (plug->tracks.count == 0)
    return;

  /* Detach audio processor from previous track */
  Track *prev = current_track();
  if (prev) {
    StopMusicStream(prev->music);
    DetachAudioStreamProcessor(prev->music.stream, process_audio);
  }

  /* Handle wraparound */

  if (index < 0)
    plug->current_track = plug->tracks.count - 1;
  else if ((size_t)index >= plug->tracks.count)
    plug->current_track = 0;
  else
    plug->current_track = index;

  /* Reset visualization buffers for clean transition */
  memset(plug->samples, 0, sizeof(plug->samples));
  memset(plug->bars, 0, sizeof(plug->bars));
  memset(plug->smear, 0, sizeof(plug->smear));
  memset(plug->spectrum, 0, sizeof(plug->spectrum));
  atomic_store(&plug->sample_write, 0);

  plug->bass_history = 0.0f;
  plug->overall_level = 0.5f;

  /* Setup and start new track */
  Track *next = current_track();
  plug->sample_rate = next->music.stream.sampleRate;
  AttachAudioStreamProcessor(next->music.stream, process_audio);
  SetMusicVolume(next->music, plug->master_vol);
  PlayMusicStream(next->music);

  plug->paused = false;
  plug->has_music = true;
}

/**
 * @brief Draws the track queue panel with scrollable list
 *
 * Displays playlist on left side in windowed mode. Features:
 * - Scrollable track list
 * - Click to select track
 * - Highlights current track
 * - Auto-scrolling text for long names
 */
static void draw_queue(void) {
  if (plug->fullscreen || !plug->has_music)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();
  float queue_width = w * 0.20f;
  float queue_height = h - 150.0f;

  /* Draw queue background */
  DrawRectangle(0, 0, (int)queue_width, (int)queue_height,
                (Color){0x15, 0x15, 0x15, 0xFF});

  /* Layout constants */
  float item_height = 50.0f;
  float font_size = 24.0f;
  float side_padding = 10.0f;
  float inner_padding = 15.0f;

  /* Handle mouse wheel scrolling */
  if (CheckCollisionPointRec(GetMousePosition(),
                             (Rectangle){0, 0, queue_width, queue_height})) {
    plug->queue_scroll += GetMouseWheelMove() * 25.0f;
  }

  /* Clamp scroll to valid range */
  float content_height = (float)plug->tracks.count * (item_height + 10.0f);
  float max_scroll = (content_height > queue_height)
                         ? (queue_height - content_height - 20.0f)
                         : 0.0f;
  if (plug->queue_scroll < max_scroll)
    plug->queue_scroll = max_scroll;
  if (plug->queue_scroll > 0)
    plug->queue_scroll = 0;

  /* Render each track item */
  for (size_t i = 0; i < plug->tracks.count; i++) {
    float y_pos = i * (item_height + 10.0f) + plug->queue_scroll + 10.0f;

    /* Skip offscreen items for performance */
    if (y_pos + item_height < 0 || y_pos > queue_height)
      continue;

    Rectangle item_rec = {side_padding, y_pos, queue_width - (side_padding * 2),
                          item_height};
    bool is_current = (i == (size_t)plug->current_track);
    bool is_hover = CheckCollisionPointRec(GetMousePosition(), item_rec);

    /* Determine item background color */
    Color base_color = is_current ? (Color){0x3b, 0x59, 0xd8, 0xFF}
                                  : (Color){0x25, 0x25, 0x25, 0xFF};
    if (is_hover && !is_current)
      base_color = (Color){0x30, 0x30, 0x30, 0xFF};

    DrawRectangleRounded(item_rec, 0.2f, 8, base_color);

    /* Extract filename from path */
    const char *name = GetFileName(plug->tracks.items[i].file_name);
    Vector2 text_size = MeasureTextEx(plug->font, name, font_size, 0);

    float available_space = item_rec.width - (inner_padding * 2);
    Vector2 text_pos = {item_rec.x + inner_padding,
                        item_rec.y + (item_rec.height / 2) - (text_size.y / 2)};

    /* Enable scissor mode for text clipping */
    BeginScissorMode((int)item_rec.x + 5, (int)item_rec.y,
                     (int)item_rec.width - 10, (int)item_rec.height);

    /* Animate text scrolling for long filenames on hover */
    if (is_hover && text_size.x > available_space) {
      float speed = 30.0f;
      float total_dist = text_size.x - available_space + 20.0f;
      float time = (float)GetTime();

      /* Ping-pong scrolling animation */
      float offset = fmodf(time * speed, total_dist * 2);
      if (offset > total_dist)
        offset = total_dist * 2 - offset;

      text_pos.x -= offset;
    } else if (text_size.x <= available_space) {
      /* Center text if it fits */
      text_pos.x = item_rec.x + (item_rec.width / 2) - (text_size.x / 2);
    }

    DrawTextEx(plug->font, name, text_pos, font_size, 0, WHITE);

    EndScissorMode();

    /* Handle track selection on click */
    if (is_hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      switch_track((int)i);
    }
  }
}

/**
 * @brief Renders frequency visualization plug->bars with advanced effects
 *
 * Draws plug->bars with:
 * - Smooth lines as plug->bars
 * - Glowing circles at tips
 * - Smear trails for motion blur effect
 * - Rainbow HSV coloring
 */
static void draw_bars(void) {
  int w = GetRenderWidth();
  int h = GetRenderHeight();

  if (!plug->has_music)
    return;

  /* Calculate bar layout based on mode */
  float start_x = plug->fullscreen ? 0 : w * 0.20f;
  float available_w = plug->fullscreen ? w : w * 0.80f;
  float cell_width = available_w / BARS;
  float base_y =
      plug->fullscreen ? (plug->mouse_active ? h * 0.95f : h) : h - 150;

  /* Static array for smear effect (motion blur) */

  /* Global visual parameters */
  float saturation = 0.75f;
  float value = 1.0f;

  /* PASS 1: Draw bar lines */
  for (int i = 0; i < BARS; i++) {
    float intensity = plug->bars[i];
    if (intensity > 1.2f)
      intensity = 1.2f;
    if (intensity < 0.0f)
      intensity = 0.0f;

    /* Update smear with slower decay */
    float smear_speed = 3.0f;
    plug->smear[i] +=
        (intensity - plug->smear[i]) * smear_speed * GetFrameTime();

    /* Calculate positions */
    float bar_height = intensity * h * (plug->fullscreen ? 0.85f : 0.6f);
    float x = start_x + i * cell_width + cell_width / 2;
    float y_top = base_y - bar_height;

    /* Rainbow color based on position */
    float hue = (float)i / BARS * 360.0f;
    Color color = ColorFromHSV(hue, saturation, value);

    /* Draw line with variable thickness */
    float thickness = cell_width / 3.0f * sqrtf(intensity);
    Vector2 start_pos = {x, y_top};
    Vector2 end_pos = {x, base_y};
    DrawLineEx(start_pos, end_pos, thickness, color);
  }

  /* Get default 1x1 white texture for shader effects */
  Texture2D default_tex = {.id = rlGetTextureIdDefault(),
                           .width = 1,
                           .height = 1,
                           .mipmaps = 1,
                           .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

  /* PASS 2: Draw smear trails (motion blur) */
  SetShaderValue(plug->circle, plug->circle_radius_location, (float[1]){0.3f},
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(plug->circle, plug->circle_power_location, (float[1]){3.0f},
                 SHADER_UNIFORM_FLOAT);

  BeginShaderMode(plug->circle);
  for (int i = 0; i < BARS; i++) {
    float intensity = plug->bars[i];
    if (intensity < 0.0f)
      intensity = 0.0f;
    if (intensity > 1.2f)
      intensity = 1.2f;

    float start_height = plug->smear[i] * h * (plug->fullscreen ? 0.85f : 0.6f);
    float end_height = intensity * h * (plug->fullscreen ? 0.85f : 0.6f);

    float x = start_x + i * cell_width + cell_width / 2;
    float y_start = base_y - start_height;
    float y_end = base_y - end_height;

    float hue = (float)i / BARS * 360.0f;
    Color color = ColorFromHSV(hue, saturation, value);

    float radius = cell_width * 1.2f * sqrtf(intensity);

    /* Draw smear as stretched texture */
    if (y_end >= y_start) {
      Rectangle dest = {x - radius / 2, y_start, radius, y_end - y_start};
      Rectangle src = {0, 0, 1, 0.5f};
      DrawTexturePro(default_tex, src, dest, (Vector2){0, 0}, 0, color);
    } else {
      Rectangle dest = {x - radius / 2, y_end, radius, y_start - y_end};
      Rectangle src = {0, 0.5f, 1, 0.5f};
      DrawTexturePro(default_tex, src, dest, (Vector2){0, 0}, 0, color);
    }
  }
  EndShaderMode();

  /* PASS 3: Draw glowing circles at bar tips */
  SetShaderValue(plug->circle, plug->circle_radius_location, (float[1]){0.07f},
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(plug->circle, plug->circle_power_location, (float[1]){5.0f},
                 SHADER_UNIFORM_FLOAT);

  BeginShaderMode(plug->circle);
  for (int i = 0; i < BARS; i++) {
    float intensity = plug->bars[i];
    if (intensity < 0.0f)
      intensity = 0.0f;
    if (intensity > 1.2f)
      intensity = 1.2f;

    float bar_height = intensity * h * (plug->fullscreen ? 0.85f : 0.6f);
    float x = start_x + i * cell_width + cell_width / 2;
    float y = base_y - bar_height;

    float hue = (float)i / BARS * 360.0f;
    Color color = ColorFromHSV(hue, saturation, value);

    /* Circle size based on intensity */
    float radius = cell_width * 0.8f * sqrtf(intensity);

    Vector2 position = {x - radius, y - radius};
    DrawTextureEx(default_tex, position, 0, 2 * radius, color);
  }
  EndShaderMode();
}

/**
 * @brief Updates visualization by computing FFT and smoothing plug->bars
 *
 * Processing pipeline:
 * 1. Apply Hann window to samples
 * 2. Compute FFT to get frequency spectrum
 * 3. Map spectrum to logarithmic frequency bins
 * 4. Apply enhanced bass boost and smoothing
 */
static void update_visualizer(void) {
  if (plug->has_music && !plug->paused) {
    // 1. Initialize Hann window (once)
    if (!plug->window_ready) {
      for (size_t i = 0; i < N; i++) {
        plug->window[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N - 1)));
      }
      plug->window_ready = true;
    }

    // 2. Apply window function
    float tmp[N];
    unsigned w =
        atomic_load_explicit(&plug->sample_write, memory_order_acquire);

    for (size_t i = 0; i < N; i++) {
      size_t idx = (w + i) % N;
      tmp[i] = plug->samples[idx] * plug->window[i];
    }

    // 3. Compute FFT
    compute_fft(tmp, 1, plug->spectrum, N);

    // 4. Find maximum amplitude for normalization
    float max_amp = 1e-6f;
    for (size_t i = 0; i < N / 2; i++) {
      float a = get_amplitude(plug->spectrum[i]);
      if (a > max_amp)
        max_amp = a;
    }

    float freq_min = 20.0f;
    float freq_max = plug->sample_rate * 0.5f;
    int bass_bands = 8;

    // FIX: current_bass_energy declared only once here
    float current_bass_energy = 0.0f;

    for (int i = 0; i < BARS; i++) {
      float t0 = (float)i / BARS;
      float t1 = (float)(i + 1) / BARS;

      float f0 = freq_min * powf(freq_max / freq_min, t0);
      float f1 = freq_min * powf(freq_max / freq_min, t1);

      size_t k0 = (size_t)(f0 * N / plug->sample_rate);
      size_t k1 = (size_t)(f1 * N / plug->sample_rate);
      if (k1 <= k0)
        k1 = k0 + 1;

      float band_max = 0.0f;
      for (size_t k = k0; k < k1 && k < N / 2; k++) {
        float a = get_amplitude(plug->spectrum[k]);
        if (a > band_max)
          band_max = a;
      }

      float normalized = band_max / max_amp;

      // 5. Bass Boost Logic
      float bass_boost = 1.0f;
      if (i < bass_bands) {
        float bass_factor = 1.0f - ((float)i / bass_bands);
        bass_boost = 1.0f + bass_factor * 3.5f;
        current_bass_energy += normalized;
      }

      float target = normalized * bass_boost;
      target = sqrtf(target);

      // FIX: Use plug->overall_level instead of static local
      plug->overall_level = 0.95f * plug->overall_level + 0.05f * normalized;
      target *= (1.0f + plug->overall_level * 0.5f);

      if (target > 1.5f)
        target = 1.5f;

      if (i == 0) {
        // FIX: Use plug->bass_history instead of static local
        plug->bass_history = 0.9f * plug->bass_history + 0.1f * target;
      }

      float dt = GetFrameTime();
      float smoothness_up = 20.0f + plug->bass_history * 10.0f;
      float smoothness_down = 4.5f + plug->bass_history * 2.0f;

      if (target > plug->bars[i]) {
        plug->bars[i] += (target - plug->bars[i]) * smoothness_up * dt;
      } else {
        plug->bars[i] += (target - plug->bars[i]) * smoothness_down * dt;
      }

      if (plug->bars[i] < 0.0f)
        plug->bars[i] = 0.0f;
      if (plug->bars[i] > 1.5f)
        plug->bars[i] = 1.5f;
    }
  }
}

/**
 * @brief Draws interactive volume slider widget
 *
 * Displays horizontal slider when hovering over volume icon.
 * Allows click-and-drag to adjust volume level.
 */
static void draw_volume_slider(void) {
  if (!plug->volume_slider.visible)
    return;

  Rectangle slider = plug->volume_slider.bounds;

  /* Draw slider background */
  DrawRectangleRec(slider, (Color){0x20, 0x20, 0x20, 0xF0});
  DrawRectangleLinesEx(slider, 1, (Color){0x50, 0x50, 0x50, 0xFF});

  /* Draw filled portion */
  float fill_width = slider.width * plug->volume_slider.value;
  Rectangle fill = {slider.x, slider.y, fill_width, slider.height};

  DrawRectangleRec(fill, (Color){100, 180, 255, 220});

  /* Draw circular indicator at current position */
  float indicator_x = slider.x + fill_width;
  float indicator_y = slider.y + slider.height * 0.5f;
  DrawCircle(indicator_x, indicator_y, 6, WHITE);
  DrawCircle(indicator_x, indicator_y, 4, (Color){100, 180, 255, 255});

  /* Draw percentage text */
  char percent_text[16];
  snprintf(percent_text, sizeof(percent_text), "%d%%",
           (int)(plug->volume_slider.value * 100));
  DrawText(percent_text, slider.x + slider.width + 10,
           slider.y + (slider.height - 10) * 0.5f, 10, WHITE);

  Vector2 mouse = GetMousePosition();

  /* Handle dragging to adjust volume */
  if (CheckCollisionPointRec(mouse, slider)) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      float relative_x = mouse.x - slider.x;
      float new_value = relative_x / slider.width;

      /* Clamp to valid range */
      if (new_value < 0.0f)
        new_value = 0.0f;
      if (new_value > 1.0f)
        new_value = 1.0f;

      plug->volume_slider.value = new_value;
      plug->master_vol = new_value;
      SetMusicVolume(current_track()->music, plug->master_vol);

      /* Update volume level icon */
      if (plug->master_vol <= 0.01f)
        plug->volume_level = 0;
      else if (plug->master_vol <= 0.65f)
        plug->volume_level = 1;
      else
        plug->volume_level = 2;
    }
  }
}

/**
 * @brief Displays tooltip text near a UI element
 *
 * @param boundary Rectangle of the UI element to attach tooltip to
 * @param label Text to display in tooltip
 */
static void tooltip(Rectangle boundary, const char *label) {
  float font_size = 30.0f;
  Vector2 text_size = MeasureTextEx(plug->font, label, font_size, 0);
  Vector2 pos;

  /* Position tooltip centered above element */
  pos.x = boundary.x + (boundary.width / 2) - (text_size.x / 2);
  pos.y = boundary.y - text_size.y - 30;

  /* Keep tooltip on screen horizontally */
  if (pos.x < 10)
    pos.x = 10;
  if (pos.x + text_size.x > GetScreenWidth() - 10) {
    pos.x = GetScreenWidth() - text_size.x - 10;
  }

  /* If no space above, show below element */
  if (pos.y < 10) {
    pos.y = boundary.y + boundary.height + 15;
  }

  /* Draw tooltip background and text */
  DrawRectangleRounded(
      (Rectangle){pos.x - 8, pos.y - 4, text_size.x + 16, text_size.y + 8},
      0.3f, 4, BLACK);

  DrawRectangleRec(
      (Rectangle){pos.x - 8, pos.y - 4, text_size.x + 16, text_size.y + 8},
      BLACK);

  DrawTextEx(plug->font, label, pos, font_size, 0, WHITE);
}

/**
 * @brief Draws UI control bar with play, volume, and fullscreen buttons
 *
 * Bar position and visibility adapts to fullscreen mode and mouse activity.
 * Displays tooltips on hover for each control.
 */
static void draw_ui_bar(void) {
  if (!plug->has_music)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();

  float bar_height = h * 0.05f;
  if (bar_height < 40.0f)
    bar_height = 40.0f;

  Rectangle bar;
  if (plug->fullscreen) {
    /* In fullscreen, only show when mouse is active */
    if (!plug->mouse_active)
      return;
    bar = (Rectangle){0, h - bar_height, (float)w, bar_height};
  } else {
    /* In windowed mode, show above progress bar */
    bar = (Rectangle){w * 0.20f, h - 150.0f - bar_height, w - w * 0.20f,
                      bar_height};
  }

  DrawRectangleRec(bar, (Color){0x10, 0x10, 0x10, 0xFF});

  /* Calculate icon sizing and spacing */
  float padding = bar.height * 0.20f;
  float icon_size = bar.height - padding * 2;
  float y = bar.y + padding;

  float x_left = bar.x + padding;

  /* Draw play/pause button */
  {
    Texture2D tex = plug->icons_textures[PLAY_UI_ICON];
    float frame = plug->paused ? 0 : 1; // Select sprite frame
    float s = (float)tex.height;
    Rectangle dst = {x_left, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    plug->ui_recs[PLAY_UI_ICON] = dst;
    x_left += icon_size + padding;
  }

  /*Draw file finder button*/
  {
    Texture2D tex = plug->icons_textures[FILE_UI_ICON];
    float frame = 0;
    float s = (float)tex.height;
    Rectangle dst = {x_left, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    plug->ui_recs[FILE_UI_ICON] = dst;
    x_left += icon_size + padding;
  }

  /* Draw volume button with slider */
  {
    Texture2D tex = plug->icons_textures[VOLUME_UI_ICON];
    float frame = (float)plug->volume_level; // 0=muted, 1=low, 2=high
    float s = (float)tex.height;
    Rectangle dst = {x_left, y, icon_size, icon_size};
    Rectangle src = {frame * s, 0, s, s};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    plug->ui_recs[VOLUME_UI_ICON] = dst;

    Vector2 mouse = GetMousePosition();

    /* Position slider next to volume icon */
    float slider_width = icon_size * 4.0f;
    float slider_height = icon_size * 0.4f;
    Rectangle slider_bounds = {dst.x + dst.width + padding,
                               dst.y + (dst.height - slider_height) * 0.5f,
                               slider_width, slider_height};

    /* Show slider when hovering over icon or slider itself */
    if (CheckCollisionPointRec(mouse, dst) ||
        CheckCollisionPointRec(mouse, slider_bounds)) {
      plug->volume_slider.visible = true;
      plug->volume_slider.bounds = slider_bounds;
      plug->volume_slider.value = (float)plug->master_vol;
    } else {
      plug->volume_slider.visible = false;
    }
    x_left += icon_size + padding;
  }

  /* Draw fullscreen toggle button (right-aligned) */
  {
    Texture2D tex = plug->icons_textures[FULLSCREEN_UI_ICON];
    float s = (float)tex.height;
    float x_right = bar.x + bar.width - padding - icon_size;
    Rectangle dst = {x_right, y, icon_size, icon_size};
    plug->ui_recs[FULLSCREEN_UI_ICON] = dst;

    bool is_hovered = CheckCollisionPointRec(GetMousePosition(), dst);
    /* Select sprite: 0=windowed, 1=windowed+hover, 2=fullscreen,
     * 3=fullscreen+hover */
    float frame =
        plug->fullscreen ? (is_hovered ? 3 : 2) : (is_hovered ? 1 : 0);

    Rectangle src = {frame * s, 0, s, s};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
  }

  /* Draw tooltips on hover */
  Vector2 mouse = GetMousePosition();
  if (CheckCollisionPointRec(mouse, plug->ui_recs[PLAY_UI_ICON])) {
    tooltip(plug->ui_recs[PLAY_UI_ICON],
            plug->paused ? "Play [SPACE]" : "Pause [SPACE]");
  } else if (CheckCollisionPointRec(mouse, plug->ui_recs[VOLUME_UI_ICON])) {
    tooltip(plug->ui_recs[VOLUME_UI_ICON],
            plug->volume_level == 0 ? "Unmute [M]" : "Mute [M]");
  } else if (CheckCollisionPointRec(mouse, plug->ui_recs[FULLSCREEN_UI_ICON])) {
    tooltip(plug->ui_recs[FULLSCREEN_UI_ICON],
            plug->fullscreen ? "Collapse [F]" : "Expand [F]");
  } else if (CheckCollisionPointRec(mouse, plug->ui_recs[FILE_UI_ICON])) {
    tooltip(plug->ui_recs[FILE_UI_ICON], "Find File [O]");
  }
}

/**
 * @brief Automatically advances to next track when current track finishes
 *
 * Checks if playback is near end of track and switches to next track in queue.
 * Only operates when music is playing and not at end of playlist.
 */
static void next_track_in_queue(void) {
  if (!plug->has_music || plug->tracks.count <= 1 || plug->paused ||
      (size_t)plug->current_track == plug->tracks.count - 1)
    return;

  float curr_time = GetMusicTimePlayed(current_track()->music);
  float total_time = GetMusicTimeLength(current_track()->music);

  /* Switch when within 0.1s of end */
  if (total_time > 0.0f && curr_time >= (total_time - 0.1f)) {
    switch_track(plug->current_track + 1);
  }
}

/**
 * @brief Checks if UI control bar should be visible
 *
 * @return true if UI bar should be drawn
 */
static bool is_ui_bar_active(void) {
  if (!plug->has_music)
    return false;

  if (plug->fullscreen)
    return plug->mouse_active;

  return true;
}

/**
 * @brief Handles keyboard and mouse input for playback controls
 *
 * Keyboard shortcuts:
 * - F: Toggle fullscreen
 * - SPACE: Play/pause
 * - M: Mute/unmute
 * - N: Next track
 * - P: Previous track
 */
static void handle_input(void) {
  if (!plug->has_music)
    return;

  /* Toggle fullscreen mode */
  if (IsKeyPressed(KEY_F)) {
    plug->fullscreen = !plug->fullscreen;
  }

  /* Toggle play/pause */
  if (IsKeyPressed(KEY_SPACE)) {

    if (plug->paused)
      ResumeMusicStream(current_track()->music);
    else
      PauseMusicStream(current_track()->music);

    plug->paused = !plug->paused;
  }

  /* Toggle mute (keyboard or click on volume icon) */
  Vector2 mouse = GetMousePosition();
  if (IsKeyPressed(KEY_M) ||
      (CheckCollisionPointRec(mouse, plug->ui_recs[VOLUME_UI_ICON]) &&
       IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
    if (plug->master_vol > 0.0f) {
      plug->volume_saved = plug->master_vol;
      plug->master_vol = 0.0f;
    } else {
      plug->master_vol =
          (plug->volume_saved > 0.0f) ? plug->volume_saved : 0.5f;
    }
    SetMusicVolume(current_track()->music, plug->master_vol);
    plug->volume_slider.value = plug->master_vol;
    plug->volume_level =
        (plug->master_vol <= 0.01f) ? 0 : (plug->master_vol <= 0.65f ? 1 : 2);
  }

  /* Next/previous track navigation */
  if (IsKeyPressed(KEY_N))
    switch_track(plug->current_track + 1);
  if (IsKeyPressed(KEY_P))
    switch_track(plug->current_track - 1);

  /* Handle UI button clicks */
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && is_ui_bar_active()) {

    if (CheckCollisionPointRec(mouse, plug->ui_recs[PLAY_UI_ICON])) {
      plug->paused = !plug->paused;

      if (plug->paused)
        PauseMusicStream(current_track()->music);
      else
        ResumeMusicStream(current_track()->music);
    }

    else if (CheckCollisionPointRec(mouse, plug->ui_recs[FULLSCREEN_UI_ICON])) {
      plug->fullscreen = !plug->fullscreen;
    }
  }
}

/**
 * @brief Handles drag-and-drop file loading
 *
 * Accepts dropped audio files and adds them to the playlist.
 * Automatically starts playback if no music was loaded.
 */

static void handle_file_drop(void) {
  if (!IsFileDropped())
    return;

  FilePathList files = LoadDroppedFiles();

  for (size_t i = 0; i < files.count; i++) {
    Music music = LoadMusicStream(files.paths[i]);
    if (!IsMusicValid(music))
      continue;

    char *file_path = strdup(files.paths[i]);
    assert(file_path);

    da_append(&plug->tracks, (CLITERAL(Track){
                                 .file_name = file_path,
                                 .music = music,
                             }));
  }

  UnloadDroppedFiles(files);

  if (!plug->has_music && plug->tracks.count > 0) {
    plug->current_track = 0;
    plug->has_music = true;
    plug->paused = false;

    memset(plug->samples, 0, sizeof(plug->samples));
    atomic_store(&plug->sample_write, 0);

    switch_track(0);
  }
}

/**
 * @brief Loads all required assets (fonts and textures)
 *
 * Loads font and UI icon textures from disk into memory.
 * Sets up texture filtering for smooth scaling.
 */
static void load_assets(void) {
  size_t data_size = 0;
  void *data = NULL;

  /* Load main UI font */
  const char *alegreya_path = "resources/fonts/Alegreya-Regular.ttf";
  data = plug_load_resoruces(alegreya_path, &data_size);
  plug->font = LoadFontFromMemory(GetFileExtension(alegreya_path), data,
                                  data_size, FONT_SIZE, NULL, 0);
  GenTextureMipmaps(&plug->font.texture);
  SetTextureFilter(plug->font.texture, TEXTURE_FILTER_BILINEAR);
  plug_free_resource(data);

  /*Load main UI shader*/
  data = plug_load_resoruces(
      TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION),
      &data_size);
  plug->circle = LoadShaderFromMemory(NULL, data);
  plug_free_resource(data);

  /* Load UI icon textures */
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

/**
 * @brief Attempts to load and append a music track to the playlist.
 * @param path The system path to the music file.
 * @return true if loaded successfully, false otherwise.
 */
static bool add_track_from_path(const char *path) {
  if (!path)
    return false;

  Music music = LoadMusicStream(path);
  if (!IsMusicValid(music)) {
    plug->error = true;
    return false;
  }

  char *path_copy = strdup(path);
  if (!path_copy)
    return false;

  da_append(&plug->tracks, (CLITERAL(Track){
                               .file_name = path_copy,
                               .music = music,
                           }));

  plug->error = false;

  return true;
}

/**
 * @brief Logic to navigate to parent directory.
 */
static void navigate_to_parent_dir(void) {
  if (strcmp(plug->current_dir, "/") == 0)
    return;

  char *last_slash = strrchr(plug->current_dir, '/');
  if (last_slash == plug->current_dir) {
    strcpy(plug->current_dir, "/");
  } else if (last_slash != NULL) {
    *last_slash = '\0';
  }

  if (plug->dir_files.count > 0)
    UnloadDirectoryFiles(plug->dir_files);
  plug->dir_files = LoadDirectoryFiles(plug->current_dir);
  plug->browser_scroll = 0;
}

/**
 * @brief Draws the internal file browser with scroll and navigation.
 */
static void draw_internal_browser(void) {
  if (!plug->show_browser)
    return;

  int w = GetRenderWidth();
  int h = GetRenderHeight();
  Rectangle browser_rec = {w * 0.1f, h * 0.1f, w * 0.8f, h * 0.8f};

  DrawRectangleRec(browser_rec, (Color){0x12, 0x12, 0x12, 0xFA});
  DrawRectangleLinesEx(browser_rec, 2, GRAY);
  DrawText(TextFormat("Browsing: %s", plug->current_dir),
           (int)browser_rec.x + 20, (int)browser_rec.y + 15, 20, SKYBLUE);

  if (plug->dir_files.count == 0) {
    plug->dir_files = LoadDirectoryFiles(plug->current_dir);
  }

  if (CheckCollisionPointRec(GetMousePosition(), browser_rec)) {
    plug->browser_scroll += (int)(GetMouseWheelMove() * 35.0f);
  }
  if (plug->browser_scroll > 0)
    plug->browser_scroll = 0;

  if (IsKeyPressed(KEY_BACKSPACE)) {
    navigate_to_parent_dir();
  }

  float item_h = 35.0f;
  int render_i = 0;

  BeginScissorMode((int)browser_rec.x, (int)browser_rec.y + 50,
                   (int)browser_rec.width, (int)browser_rec.height - 60);

  for (size_t i = 0; i < plug->dir_files.count; i++) {
    const char *path = plug->dir_files.paths[i];
    bool is_dir = DirectoryExists(path);
    bool is_music =
        IsFileExtension(path, ".mp3") || IsFileExtension(path, ".wav") ||
        IsFileExtension(path, ".ogg") || IsFileExtension(path, ".flac");
    if (!is_dir && !is_music)
      continue;
    const char *file_name = GetFileName(path);
    if (file_name[0] == '.' && strcmp(file_name, "..") != 0)
      continue;
    Rectangle item_r = {browser_rec.x + 10,
                        browser_rec.y + 60 + (render_i * item_h) +
                            plug->browser_scroll,
                        browser_rec.width - 20, item_h};
    render_i++;

    bool hovered = CheckCollisionPointRec(GetMousePosition(), item_r);
    if (hovered) {
      DrawRectangleRec(item_r, (Color){0x30, 0x30, 0x30, 0xFF});
      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (is_dir) {
          const char *dir_name = GetFileName(path);
          if (strcmp(dir_name, "..") == 0) {
            navigate_to_parent_dir();
          } else if (strcmp(dir_name, ".") != 0) {
            snprintf(plug->current_dir, sizeof(plug->current_dir), "%s", path);
            UnloadDirectoryFiles(plug->dir_files);
            plug->dir_files = LoadDirectoryFiles(plug->current_dir);
            plug->browser_scroll = 0;
          }
          break;
        } else {
          if (add_track_from_path(path)) {
            if (!plug->has_music) {
              // Primera canción: siempre reproducir
              plug->has_music = true;
              switch_track(0);
            }
            // Si ya hay música, simplemente agregar a la cola
            // No cambiar automáticamente de track

            plug->show_browser = false;
          }
        }
      }
    }

    const char *label =
        is_dir ? TextFormat("[DIR] %s", GetFileName(path)) : GetFileName(path);
    DrawText(label, (int)item_r.x + 10, (int)item_r.y + 8, 18,
             is_dir ? GOLD : WHITE);
  }
  EndScissorMode();

  if (IsKeyPressed(KEY_ESCAPE))
    plug->show_browser = false;
}

static void handle_file_inputs(void) {
  Vector2 mouse = GetMousePosition();

  // plug->ui_recs[FILE_UI_ICON] is defined in draw_ui_bar
  bool icon_clicked =
      CheckCollisionPointRec(mouse, plug->ui_recs[FILE_UI_ICON]) &&
      IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

  if (IsKeyPressed(KEY_O) || icon_clicked) {
    plug->show_browser = !plug->show_browser;
    if (plug->show_browser) {
      // Clean up old list and force fresh reload from current_dir
      if (plug->dir_files.count > 0)
        UnloadDirectoryFiles(plug->dir_files);
      plug->dir_files = LoadDirectoryFiles(plug->current_dir);
      plug->browser_scroll = 0;
    }
  }
}

/**
 * @brief Handles the external file dialog using tinyfiledialogs.
 * Opens by default in the user's Music folder.
 */
static void handle_tiny_dialogs_open(void) {
  int w = GetRenderWidth();
  int h = GetRenderHeight();
  const char *filters[] = {"*.wav", "*.ogg", "*.mp3", "*.flac"};
  const char *path = NULL;

  // Logic for first-time load (empty state)
  if (!plug->has_music) {
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      const char *home = getenv("HOME");
      char default_dir[512] = "./"; // Fallback to current directory

      if (home) {
        /* * NOTE: On Arch Linux, the folder is usually 'Music' (English).
         * Change to "Musica" only if your folder is named that way.
         */
        snprintf(default_dir, sizeof(default_dir), "%s/Music/", home);

        // Verification: If 'Music' doesn't exist, try 'Musica'
        if (!DirectoryExists(default_dir)) {
          snprintf(default_dir, sizeof(default_dir), "%s/Musica/", home);
        }

        // Final fallback if neither exists
        if (!DirectoryExists(default_dir)) {
          snprintf(default_dir, sizeof(default_dir), "%s/", home);
        }
      }

      path =
          tinyfd_openFileDialog("Select Music", default_dir, ARRAY_LEN(filters),
                                filters, "Music Files", 0);

      if (add_track_from_path(path)) {
        plug->has_music = true;
        switch_track(0);
      }
    } else {
      /* Draw central call-to-action message */
      const char *msg = plug->error ? "Error: Could not load file"
                                    : "Click to Select File\n(Or Drag & Drop)";
      Color col = plug->error ? RED : WHITE;
      Vector2 size =
          MeasureTextEx(plug->font, msg, (float)plug->font.baseSize, 0);
      Vector2 pos = {(w - size.x) / 2.0f, (h - size.y) / 2.0f};

      DrawTextEx(plug->font, msg, pos, (float)plug->font.baseSize, 0, col);
    }
    return;
  }
}

/**
 * @brief Unloads all assets from memory
 *
 * Frees font and texture resources. Called before hot reload.
 */

static void unload_assets(void) {
  UnloadFont(plug->font);
  UnloadShader(plug->circle);

  for (Ui_Icon icon = 0; icon < COUNT_UI_ICONS; icon++) {
    UnloadTexture(plug->icons_textures[icon]);
  }
}

/**
 * @brief Hot reload callback - restores state after code reload
 *
 * @param prev Pointer to plugin state from before reload
 */
void plug_post_reload(Plug *prev) {
  plug = prev;
  if (plug->has_music) {
    AttachAudioStreamProcessor(current_track()->music.stream, process_audio);
  }
  load_assets();
}

/**
 * @brief Hot reload callback - saves state before code reload
 *
 * @return Pointer to plugin state to preserve
 */
Plug *plug_pre_reload(void) {
  if (plug->has_music) {
    DetachAudioStreamProcessor(current_track()->music.stream, process_audio);
  }
  unload_assets();

  return plug;
}

/**
 * @brief Initializes plugin state and resources
 *
 * Called once at application startup. Sets up:
 * - Plugin state structure
 * - Default settings
 * - Audio buffers
 * - Assets loading
 */
void plug_init(void) {
  plug = calloc(1, sizeof(*plug));
  assert(plug);

  memset(plug, 0, sizeof(*plug));

  load_assets();
  plug->fullscreen = false;
  plug->mouse_active = false;
  plug->last_mouse_move_time = -100.0f;
  plug->volume_level = 1;
  plug->sample_write = 0;
  plug->master_vol = 0.5f;
  plug->volume_saved = 0;
  plug->window_ready = false;

  plug->bass_history = 0.0f;
  plug->overall_level = 0.5f;
  const char *home = getenv("HOME");
  if (home) {
    snprintf(plug->current_dir, sizeof(plug->current_dir), "%s/Musica", home);
  } else {
    strcpy(plug->current_dir, "."); // Fallback to current directory
  }
  /* Initialize audio processing buffers */
  memset(plug->samples, 0, sizeof(plug->samples));
  memset(plug->bars, 0, sizeof(plug->bars));
  memset(&plug->volume_slider, 0, sizeof(plug->volume_slider));
  memset(plug->smear, 0, sizeof(plug->smear));
  memset(plug->spectrum, 0, sizeof(plug->spectrum));
  SetMasterVolume(plug->master_vol);
  SetTargetFPS(60);
}

/**
 * @brief Main update loop - called every frame
 *
 * Handles:
 * - Music stream updates
 * - File dialog for initial load
 * - Input processing
 * - Audio visualization
 * - Rendering all UI elements
 */
void plug_update(void) {
  /* Update audio stream */
  if (plug->has_music && !plug->paused) {
    UpdateMusicStream(current_track()->music);
  }

  /* Process input and state updates */
  handle_tiny_dialogs_open();
  update_mouse_state();
  handle_input();
  handle_file_drop();
  next_track_in_queue();

  handle_file_inputs();
  /* Render frame */
  BeginDrawing();
  ClearBackground((Color){0x18, 0x18, 0x18, 0xFF});

  update_visualizer();
  draw_queue();

  draw_progress();
  draw_bars();
  draw_ui_bar();
  draw_volume_slider();

  draw_internal_browser();
  EndDrawing();
}
