# Musualizer

A music visualizer application built with Raylib that provides real-time audio visualization with an integrated file browser and playback controls.


## Features

- 🎵 Real-time audio visualization
- 📁 Built-in file browser for music selection
- ⌨️ Comprehensive keyboard shortcuts
- 🔄 Hot reload support for development
- 🎨 Fullscreen mode support

## Quick Start

### Building the Application

```bash
$ make  
$ ./build/music
```

### Hot Reloading (Development Mode)

For development with hot reloading enabled:

```bash
$ export HOTRELOAD=1
$ export LD_LIBRARY_PATH="./build/:$LD_LIBRARY_PATH"
$ make 
$ ./build/musualizer
```

## Keyboard Shortcuts

### General Playback

| Key | Action |
|-----|--------|
| `SPACE` | Play / Pause |
| `M` | Mute / Unmute Toggle |
| `N` | Next Track in Playlist |
| `P` | Previous Track in Playlist |
| `F` | Toggle Fullscreen Mode |

### Internal File Browser

Access the file browser by pressing `0` (zero) or the folder icon key.

| Key | Action |
|-----|--------|
| `0` | Toggle File Browser |
| `BACKSPACE` | Navigate to Parent Directory (Go Up) |
| `ESC` | Close Browser |
| `MOUSE WHEEL` | Scroll through file list |
| `LEFT CLICK` | Select track or enter folder |

## Export Options

Both the General Playback and Internal File Browser sections include an "Export to Spreadsheet" option for exporting shortcut configurations.

## Requirements

- C compiler (GCC recommended)
- Raylib library
- POSIX-compatible system (Linux/macOS)

## References

This project is built upon the following resources:

- [nob.h](https://github.com/tsoding/nob.h/blob/main/nob.h) - Build system
- [Raylib](https://www.raylib.com/) - Graphics and audio library
- [Musializer](https://github.com/tsoding/musializer/blob/9d822424be0d555ab70d4e9356ba26e3e52b1916/src/musializer.c) - Original inspiration
- [FFT Implementation in C](https://github.com/muditbhargava66/FFT-implementation-in-C/tree/main/fft) - Fast Fourier Transform algorithm

## Project Structure

```
.
├── build/              # Compiled binaries
├── build.sh           # Build script
├── src/               # Source files
└── README.md          # This file
```

## Development

The application supports hot reloading during development. When `HOTRELOAD=1` is set, you can modify the code and rebuild without restarting the application.

## License

Please refer to the original project licenses for the components used in this application.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

---

**Note:** This is a music visualization application. Make sure you have the appropriate audio files and codecs installed on your system for optimal playback.
