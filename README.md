# DownPlay

DownPlay is a YouTube video and playlist downloader built on `yt-dlp`. It supports a normal Python development workflow and a `py2c` build pipeline that compiles the CLI through Cython into native executables.

## Guide for Non-Technical Users

This section is for users who only want to download videos and do not care about Python, Cython, compilers, or build tools.

### What You Need

You only need the ready-made DownPlay executable, which you can download from the [GitHub Releases](../../releases) page:

For Linux:

```text
downplay-linux-x86_64
```

For Windows:

```text
downplay-windows-x64.exe
```

*(You can rename these to `downplay` or `downplay.exe` for convenience after downloading).*

You do not need to install:

- Python
- pip
- yt-dlp
- Cython
- ffmpeg
- any compiler

### How to Run It

Run it directly

##OR

Open a terminal in the folder where the `downplay` file is located.

###On Linux:

Step 1:

Giving execute permissions to the executable file

```sh
chmod +x downplay-linux-x86_64
```

Step 2:

```sh
./downplay-linux-x86_64 -u "PASTE_YOUTUBE_LINK_HERE" -o downloads
```

On Windows Command Prompt:

```bat
downplay-windows-x64.exe -u "PASTE_YOUTUBE_LINK_HERE" -o downloads
```

On Windows PowerShell:

```powershell
.\downplay-windows-x64.exe -u "PASTE_YOUTUBE_LINK_HERE" -o downloads
```

Example:

Linux:

```sh
./downplay-linux-x86_64 -u "https://www.youtube.com/watch?v=VIDEO_ID" -o downloads
```

Windows Command Prompt example:

```bat
downplay-windows-x64.exe -u "https://www.youtube.com/watch?v=VIDEO_ID" -o downloads
```

Windows PowerShell example:

```powershell
.\downplay-windows-x64.exe -u "https://www.youtube.com/watch?v=VIDEO_ID" -o downloads
```

For a playlist:

```sh
./downplay -u "https://www.youtube.com/playlist?list=PLAYLIST_ID" -o downloads
```

Windows PowerShell playlist example:

```powershell
.\downplay.exe -u "https://www.youtube.com/playlist?list=PLAYLIST_ID" -o downloads
```

DownPlay will automatically detect whether the link is a single video or a playlist.

### Where Files Are Saved

Use `--output` to choose where downloads go:

```sh
./downplay -u "PASTE_YOUTUBE_LINK_HERE" -o MyVideos
```

Windows PowerShell:

```powershell
.\downplay.exe -u "PASTE_YOUTUBE_LINK_HERE" -o MyVideos
```

If the folder does not exist, DownPlay creates it.

If you do not provide `--output`, files are saved in the current folder.

### What You Will See

During a download, DownPlay shows:

- download percentage
- speed
- estimated time remaining
- completed file name
- final summary

### Common Problems

If the download fails, the most common reasons are:

- the YouTube link is private, deleted, age-restricted, or region-blocked
- YouTube changed something and the downloader needs an update
- the video is only available as separate audio/video streams, which this app intentionally avoids
- your internet connection is unstable

If no file downloads, try another public YouTube video first to confirm the app works.

### Important Limitation

DownPlay downloads progressive formats only, meaning video and audio must already be in one file. It does not merge separate audio and video streams. This keeps the app simple and avoids requiring ffmpeg, but some YouTube videos may not be downloadable in the requested format.

## Features

- Download a single video or a full playlist.
- Auto-detect video vs playlist URLs.
- Uses progressive formats only: video and audio in the same file.
- Avoids ffmpeg merging and post-processing.
- Provides visible progress, speed, ETA, and final summary.
- Supports native debug and release builds through `py2c`.

## Requirements

For Python development:

- Python 3.10+
- `yt-dlp`

For native builds:

- Python 3 with `venv`
- C compiler: `gcc`/`cc` on Linux, Visual Studio Build Tools or MinGW on Windows
- `make` on Linux for the wrapper bootstrap

`py2c` can create the build virtual environment and install Python dependencies automatically.

## Quick Start

Run directly with Python:

```sh
python3 src/main.py -u "https://www.youtube.com/watch?v=VIDEO_ID" -o downloads
```

Install dependencies manually if needed:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## CLI Usage

```sh
python3 src/main.py -u URL [-o DIR] [--mode video|playlist|auto] [-v]
```

Options:

- `-u, --url`: YouTube video or playlist URL.
- `-o, --output`: Output directory. Defaults to the current directory.
- `--mode`: `video`, `playlist`, or `auto`. Defaults to `auto`.
- `-v, --version`: Show the app version and author, then exit.

Example:

```sh
python3 src/main.py -u "https://www.youtube.com/playlist?list=PLAYLIST_ID" -o downloads --mode playlist
```

## Native Builds

Linux/macOS entrypoint:

```sh
./py2c <command>
```

Windows entrypoint:

```bat
py2c.bat <command>
```

### Dependency Bootstrap

Initialize the build venv and install or refresh dependencies:

```sh
./py2c installDeps
```

Windows:

```bat
py2c.bat installDeps
```

Force dependency refresh during a build:

```sh
./py2c --auto-install assembleDebug
./py2c --auto-install assembleRelease
```

### Debug Build

```sh
./py2c assembleDebug
```

Output:

```text
build/downplay
```

`assembleDebug` is for development and inspection:

- Uses live source and `.build-venv/site-packages`.
- Enables Python fault handler, unbuffered output, and tracemalloc.
- Prints py2c boundary diagnostics.
- Keeps Python stack traces and runtime behavior visible.
- Compiles with debug-oriented native flags.

`./py2c build` is an alias for `assembleDebug`.

### Release Build

```sh
./py2c assembleRelease
```

Output:

```text
release/downplay
```

`assembleRelease` is the production target:

- Produces a single ready-to-run executable on Linux.
- Produces a single ready-to-run `downplay.exe` on Windows.
- Requires no Python installation for the end user.
- Requires no pip install or manual library setup.
- Embeds the Cython-built binary, Python runtime, stdlib, native modules, `libpython`, and installed site-packages.
- Hides py2c, Cython, and Python internals from normal runtime output.
- Uses optimized native build flags.

The release executable extracts its private runtime internally before launching the compiled application.

## Releases & CI/CD

DownPlay uses GitHub Actions to fully automate its release engineering:
- On every push of a new version tag (e.g., `v1.0.0`), the [release.yml](.github/workflows/release.yml) workflow triggers automatically.
- It spins up isolated Ubuntu and Windows runners.
- The pipeline securely bootstraps the `py2c` C compiler toolchain, Python environment, and dependencies (`yt-dlp`).
- It seamlessly bundles everything into architecture-specific, zero-dependency, single-file executables (`downplay-linux-x86_64` and `downplay-windows-x64.exe`).
- The final artifacts are published directly to the GitHub Releases page.

## Project Layout

```text
src/main.py                    Python CLI entrypoint
src/wrapper/py2c.c             Linux py2c build wrapper
src/wrapper/onefile_launcher.c Single-file release launcher
src/wrapper/onefile_launcher_windows.c Windows one-file release launcher
src/wrapper/libft/             C helper library used by py2c
py2c                           Linux/macOS wrapper bootstrap
py2c.bat                       Windows wrapper entrypoint
py2c_windows.py                Windows py2c build implementation
requirements.txt               Python runtime dependencies
PY2C_TARGETS.md                Detailed target contract
```

## Cleaning

Remove generated build artifacts and build venv:

```sh
./py2c clean
```

## Notes

- Download behavior depends on YouTube and `yt-dlp` support for the provided URL.
- The app intentionally selects progressive formats only, so some videos may fail if YouTube only exposes separate audio/video streams for the requested content.
- Generated outputs in `build/`, `release/`, and `.build-venv/` are intentionally ignored by git.
