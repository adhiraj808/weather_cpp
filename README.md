# Live Weather CLI

A lightweight C++ terminal weather app that fetches live conditions from OpenWeather, renders animated ASCII weather scenes, and can play contextual audio cues.

## Features

- Live weather lookup by manual city input or CLI argument
- Privacy-first flow with no IP geolocation and no location caching
- ANSI-based terminal rendering with cursor control and frame buffering
- Animated weather scenes for sunny, rainy, snowy, thunderstorm, and cloudy conditions
- Optional audio cues with best-effort cross-platform playback
- Minimal dependency surface: C++, `curl`, and platform audio support

## Demo Preview

```text
Live Weather CLI
==============================================================================
City: New Delhi, IN         Temp: 28.4 C      Weather: Thunderstorm
Description: Light Rain     Next refresh: 233s Audio: enabled
Updated at: 2026-04-06 19:15:02
------------------------------------------------------------------------------
                           .--.
                        .-(    ).
                       (___.__)__)
                            /
                           /_
                          / /
                    lightning and thunder nearby
------------------------------------------------------------------------------
Status: Live data refreshed successfully.
```

For a repository demo, add a short terminal GIF showing one sunny cycle and one storm cycle.

## Project Layout

```text
weather_cpp/
|-- weather.cpp
|-- README.md
|-- .gitignore
|-- assets/
|   `-- audio/
|       `-- README.md
`-- sounds/                # legacy audio folder, still supported
```

## Dependencies

- C++ compiler with C++11 or newer support
- `curl` available on the command line
- OpenWeather API key
- Optional audio tools:
  - Windows: WinMM via `-lwinmm`
  - macOS: `afplay`
  - Linux: `paplay`, `aplay`, or `ffplay`

## API Setup

1. Create an OpenWeather API key.
2. Export it as `OPENWEATHER_API_KEY`.

Windows PowerShell:

```powershell
$env:OPENWEATHER_API_KEY="your_api_key_here"
```

Linux/macOS:

```bash
export OPENWEATHER_API_KEY="your_api_key_here"
```

You can also pass the key directly with `--api-key`.

## Build

Windows:

```powershell
g++ weather.cpp -o weather.exe -lwinmm
```

Linux/macOS:

```bash
g++ weather.cpp -o weather
```

## Usage

Prompt for city:

```powershell
.\weather.exe
```

Pass the city directly:

```powershell
.\weather.exe --city "New Delhi"
```

Or use positional city input:

```powershell
.\weather.exe "San Francisco"
```

Disable audio:

```powershell
.\weather.exe --city "London" --no-audio
```

Tweak refresh and animation rate:

```powershell
.\weather.exe --city "Tokyo" --refresh 180 --fps 8
```

## Audio Assets

Recommended location:

- `assets/audio/sunny.wav`
- `assets/audio/rainy.wav`
- `assets/audio/snow.wav`
- `assets/audio/thunderstorm.wav`
- `assets/audio/cloudy.wav`

Legacy compatibility is also enabled for:

- `sounds/cloudy.mp3`
- `sounds/storm.mp3`
- similar `.mp3` or `.wav` files using `sunny`, `clear`, `rainy`, `rain`, `snow`, `thunderstorm`, or `cloudy`

## Privacy Notes

- No IP lookup
- No user location storage
- No JSON cache files written during runtime
- City selection happens only from user input or CLI arguments

## Language Evaluation

C++ is suitable for this project because it keeps the binary lightweight, gives direct terminal and platform API access, and performs well for animation loops.

If the project grows into a richer cross-platform TUI with stronger process handling and easier packaging, Rust would be the strongest next step. It offers better safety than C++ while still keeping terminal rendering and audio integration performant.
