#!/usr/bin/env python3
"""
YouTube Playlist Downloader CLI
Supports progressive fallback downloads and high-quality streams merging using FFmpeg.
Includes an interactive guided menu and step-by-step terminal downloader.
"""

import argparse
import os
import sys
import platform
import shutil
import urllib.request
import zipfile
import tarfile
from pathlib import Path

APP_NAME = "DownPlay"
APP_VERSION = "1.0.0"
APP_AUTHOR = "SoufianoDev"

# Local FFmpeg configuration path
FFMPEG_DIR = Path.home() / ".downplay" / "bin"


def get_ytdlp():
    """Import yt_dlp only when the app actually needs it."""
    try:
        import yt_dlp
        return yt_dlp
    except ModuleNotFoundError:
        return None


def check_ffmpeg() -> tuple[bool, Path | None]:
    """
    Checks if FFmpeg and FFprobe are available on the system PATH or the local ~/.downplay/bin folder.
    Returns (is_available, local_bin_dir).
    """
    # 1. Check system PATH
    ffmpeg_in_path = shutil.which("ffmpeg")
    ffprobe_in_path = shutil.which("ffprobe")
    if ffmpeg_in_path and ffprobe_in_path:
        return True, None

    # 2. Check local folder
    suffix = ".exe" if platform.system() == "Windows" else ""
    local_ffmpeg = FFMPEG_DIR / f"ffmpeg{suffix}"
    local_ffprobe = FFMPEG_DIR / f"ffprobe{suffix}"
    if local_ffmpeg.exists() and local_ffprobe.exists():
        return True, FFMPEG_DIR

    return False, None


def download_and_install_ffmpeg() -> bool:
    """
    Downloads static FFmpeg & FFprobe binaries from yt-dlp/FFmpeg-Builds,
    unpacks them programmatically, and places them into ~/.downplay/bin.
    """
    FFMPEG_DIR.mkdir(parents=True, exist_ok=True)
    sys_plat = platform.system()
    sys_arch = platform.machine()
    
    # Select release archive based on platform
    if sys_plat == "Windows":
        url = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
        archive_name = "ffmpeg.zip"
    elif sys_plat == "Linux":
        if "arm" in sys_arch.lower() or "aarch64" in sys_arch.lower():
            url = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl.tar.xz"
        else:
            url = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz"
        archive_name = "ffmpeg.tar.xz"
    elif sys_plat == "Darwin":
        url = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-osx64-gpl.zip"
        archive_name = "ffmpeg.zip"
    else:
        print(f"✗ Unsupported platform: {sys_plat}")
        return False
    
    archive_path = FFMPEG_DIR / archive_name
    
    try:
        print(f"\nDownloading static FFmpeg for {sys_plat} ({sys_arch})...")
        req = urllib.request.Request(
            url,
            headers={'User-Agent': 'Mozilla/5.0'}
        )
        with urllib.request.urlopen(req) as response:
            total_size = int(response.info().get('Content-Length', 0))
            downloaded = 0
            block_size = 8192
            with open(archive_path, 'wb') as f:
                while True:
                    buffer = response.read(block_size)
                    if not buffer:
                        break
                    downloaded += len(buffer)
                    f.write(buffer)
                    if total_size > 0:
                        percent = downloaded * 100 / total_size
                        print(f"\r  Downloading: {percent:.1f}% ({downloaded / 1024 / 1024:.1f}MB / {total_size / 1024 / 1024:.1f}MB)", end='', flush=True)
            print("\n  ✓ Download complete.")
        
        print("Extracting FFmpeg & FFprobe binaries...")
        if archive_name.endswith(".zip"):
            with zipfile.ZipFile(archive_path, 'r') as zip_ref:
                for member in zip_ref.namelist():
                    filename = os.path.basename(member)
                    if filename in ('ffmpeg.exe', 'ffprobe.exe', 'ffmpeg', 'ffprobe'):
                        source = zip_ref.open(member)
                        target_path = FFMPEG_DIR / filename
                        with source, open(target_path, "wb") as target:
                            target.write(source.read())
                        if sys_plat != "Windows":
                            os.chmod(target_path, 0o755)
        else:  # .tar.xz
            with tarfile.open(archive_path, 'r:xz') as tar_ref:
                for member in tar_ref.getmembers():
                    filename = os.path.basename(member.name)
                    if filename in ('ffmpeg', 'ffprobe') and member.isfile():
                        source = tar_ref.extractfile(member)
                        if source:
                            target_path = FFMPEG_DIR / filename
                            with open(target_path, "wb") as target:
                                target.write(source.read())
                            os.chmod(target_path, 0o755)
        
        print("✓ Extraction complete. FFmpeg is fully configured.")
        if archive_path.exists():
            archive_path.unlink()
        return True
    except Exception as e:
        print(f"\n✗ Installation failed: {e}")
        if archive_path.exists():
            archive_path.unlink()
        return False


def parse_args():
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description='Download YouTube videos and playlists using yt-dlp',
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        '-u', '--url',
        required=False,
        help='YouTube video or playlist URL'
    )
    parser.add_argument(
        '-o', '--output',
        default='.',
        help='Output directory for downloads (default: current folder)'
    )
    parser.add_argument(
        '--mode',
        choices=['video', 'playlist', 'auto'],
        default='auto',
        help='Download mode: video, playlist, or auto-detect (default: auto)'
    )
    parser.add_argument(
        '-v', '--version',
        action='version',
        version=f'{APP_NAME} {APP_VERSION} by {APP_AUTHOR}'
    )
    return parser.parse_args()


def detect_mode(url):
    """Detect if URL is a playlist or single video using yt-dlp."""
    yt_dlp = get_ytdlp()
    if yt_dlp is None:
        return 'video'
    try:
        with yt_dlp.YoutubeDL({
            'quiet': True,
            'no_warnings': True,
            'extract_flat': True
        }) as ydl:
            info = ydl.extract_info(url, download=False)
            if info and info.get('_type') == 'playlist':
                return 'playlist'
            return 'video'
    except Exception:
        return 'video'


def create_progress_hook(tracker):
    """Factory function to create a progress hook with access to tracker."""
    def progress_hook(d):
        if d['status'] == 'downloading':
            total = d.get('total_bytes') or d.get('total_bytes_estimate')
            if total and total > 0:
                percent = min(100.0, d['downloaded_bytes'] / total * 100)
            else:
                percent = 0.0
            
            speed = d.get('speed')
            if speed and speed > 0:
                if speed >= 1024 * 1024:
                    speed_str = f"{speed / 1024 / 1024:.2f} MB/s"
                elif speed >= 1024:
                    speed_str = f"{speed / 1024:.2f} KB/s"
                else:
                    speed_str = f"{speed:.0f} B/s"
            else:
                speed_str = "N/A"
            
            eta = d.get('eta')
            eta_str = f"{int(eta)}s" if eta is not None and eta >= 0 else "N/A"
            
            print(f"\r  [{percent:5.1f}%] | {speed_str} | ETA: {eta_str}", end='', flush=True)
        
        elif d['status'] == 'finished':
            filename = os.path.basename(d.get('filename', 'Unknown'))
            print(f"\n  ✓ Complete: {filename}")
            tracker['success'] += 1
        
        elif d['status'] == 'error':
            error_msg = d.get('error', 'Unknown error')
            print(f"\n  ✗ Error: {error_msg}")
            tracker['fail'] += 1
    
    return progress_hook


def build_ytdlp_options(args, mode, tracker):
    """Build yt-dlp options dictionary based on arguments, mode, and FFmpeg availability."""
    os.makedirs(args.output, exist_ok=True)
    
    if mode == 'playlist':
        outtmpl = os.path.join(args.output, '%(playlist_index)02d - %(title)s.%(ext)s')
    else:
        outtmpl = os.path.join(args.output, '%(title)s.%(ext)s')
    
    # Dynamic FFmpeg selection
    has_ffmpeg, ffmpeg_dir = check_ffmpeg()
    
    if has_ffmpeg:
        # High quality: Download best separate tracks and merge them cleanly
        format_selector = 'bestvideo+bestaudio/best'
        merge_format = 'mp4'
        postprocessors = [{
            'key': 'FFmpegMerger',
            'already_have_file': False,
        }]
        ffmpeg_loc = str(ffmpeg_dir) if ffmpeg_dir else None
    else:
        # Graceful fallback: progressive streams only (no external tool requirements)
        format_selector = 'best[vcodec!=none][acodec!=none]'
        merge_format = None
        postprocessors = []
        ffmpeg_loc = None
    
    options = {
        'format': format_selector,
        'outtmpl': outtmpl,
        'progress_hooks': [create_progress_hook(tracker)],
        'no_post_overwrites': True,
        'ignoreerrors': True,
        'quiet': False,
        'no_warnings': False,
        'ffmpeg_location': ffmpeg_loc,
        'postprocessors': postprocessors,
        'merge_output_format': merge_format,
    }
    
    if mode == 'video':
        options['noplaylist'] = True
    else:
        options['noplaylist'] = False
    
    return options


def download(url, options):
    """Execute download with yt-dlp and return success status."""
    yt_dlp = get_ytdlp()
    if yt_dlp is None:
        print("✗ yt-dlp is not installed or not available in the current environment.")
        return False
    try:
        with yt_dlp.YoutubeDL(options) as ydl:
            return ydl.download([url]) == 0
    except yt_dlp.utils.DownloadError as e:
        print(f"✗ Download error: {e}")
        return False
    except KeyboardInterrupt:
        print("\n\n⚠ Download interrupted by user")
        return False
    except Exception as e:
        print(f"✗ Unexpected error: {type(e).__name__}: {e}")
        return False


def run_interactive_download():
    """Step-by-step guided download dialog."""
    print("\n" + "-" * 60)
    print("  DOWNLOAD GUIDE")
    print("-" * 60)
    
    url = ""
    while not url:
        url = input("  Enter YouTube Video or Playlist URL: ").strip()
        if not url:
            print("  ✗ URL cannot be empty.")
    
    output_dir = input("  Enter output folder [default: current directory]: ").strip()
    if not output_dir:
        output_dir = "."
    
    print("\n  Select Download Mode:")
    print("    [1] Auto-detect (Recommended)")
    print("    [2] Video only")
    print("    [3] Playlist only")
    mode_choice = input("  Select [1-3] (default: 1): ").strip()
    if mode_choice == '2':
        mode = 'video'
    elif mode_choice == '3':
        mode = 'playlist'
    else:
        mode = 'auto'
    
    print("\n" + "-" * 60)
    print("  Summary:")
    print(f"    URL:    {url}")
    print(f"    Folder: {os.path.abspath(output_dir)}")
    print(f"    Mode:   {mode}")
    print("-" * 60)
    
    # FFmpeg dynamic check & guided choice
    has_ffmpeg, _ = check_ffmpeg()
    if not has_ffmpeg:
        print("\n  [!] FFmpeg is missing. Quality is capped at progressive streams (max 720p).")
        install_choice = input("  Would you like to automatically download and install FFmpeg now? [y/N]: ").strip().lower()
        if install_choice == 'y':
            download_and_install_ffmpeg()
            
    proceed = input("\n  Start download? [Y/n]: ").strip().lower()
    if proceed == 'n':
        print("\n  Download cancelled.")
        input("\n  Press Enter to return to the menu...")
        return
        
    print("\n  Initializing download...\n")
    
    class ArgsMock:
        def __init__(self, url, output, mode):
            self.url = url
            self.output = output
            self.mode = mode
            
    args = ArgsMock(url, output_dir, mode)
    tracker = {'success': 0, 'fail': 0}
    
    if mode == 'auto':
        final_mode = detect_mode(url)
        print(f"  Mode auto-detected as '{final_mode}'")
    else:
        final_mode = mode
        
    options = build_ytdlp_options(args, final_mode, tracker)
    
    try:
        download(url, options)
    except KeyboardInterrupt:
        print("\n  ⚠ Interrupted")
        
    total = tracker['success'] + tracker['fail']
    print(f"\n{'='*60}")
    print("DOWNLOAD SUMMARY")
    print(f"{'='*60}")
    print(f"  Successful downloads: {tracker['success']}")
    print(f"  Failed downloads:     {tracker['fail']}")
    print(f"  Total items:          {total}")
    print(f"{'='*60}")
    
    input("\n  Press Enter to return to the main menu...")


def run_ffmpeg_manager():
    """FFmpeg utility configuration manager."""
    while True:
        os.system('cls' if os.name == 'nt' else 'clear')
        has_ffmpeg, ffmpeg_dir = check_ffmpeg()
        suffix = ".exe" if platform.system() == "Windows" else ""
        
        print("=" * 60)
        print("                 M A N A G E   F F M P E G")
        print("=" * 60)
        
        if has_ffmpeg:
            loc = ffmpeg_dir if ffmpeg_dir else "System PATH"
            print(f"  Status: INSTALLED")
            print(f"  Location: {loc}")
            print("\n  [1] Reinstall / Update FFmpeg")
            print("  [2] Uninstall Local FFmpeg")
            print("  [3] Back to Main Menu")
        else:
            print("  Status: NOT INSTALLED")
            print("  (Quality will be limited to max 720p progressive streams)")
            print("\n  [1] Automatically Download and Install FFmpeg")
            print("  [2] Back to Main Menu")
            
        choice = input("\n  Select an option: ").strip()
        
        if choice == '1':
            download_and_install_ffmpeg()
            input("\n  Press Enter to continue...")
        elif choice == '2' and has_ffmpeg:
            confirm = input("  Are you sure you want to delete local FFmpeg binaries? [y/N]: ").strip().lower()
            if confirm == 'y':
                for name in ('ffmpeg', 'ffprobe', f'ffmpeg{suffix}', f'ffprobe{suffix}'):
                    p = FFMPEG_DIR / name
                    if p.exists():
                        p.unlink()
                print("  ✓ Local FFmpeg binaries deleted successfully.")
            input("\n  Press Enter to continue...")
        else:
            break


def run_interactive_help():
    """Displays interactive CLI usage instructions."""
    print("\n" + "=" * 60)
    print("  CLI USAGE INFORMATION")
    print("=" * 60)
    print("  DownPlay can be operated directly from your terminal using flags.")
    print("\n  Usage:")
    print("    downplay -u <URL> [-o <OUTPUT_DIR>] [--mode <video|playlist|auto>]")
    print("\n  Flags:")
    print("    -u, --url       YouTube video or playlist URL (Required)")
    print("    -o, --output    Directory to save downloads (default: current directory)")
    print("    --mode          Download mode: video, playlist, or auto (default: auto)")
    print("    -v, --version   Display application version and author")
    print("    -h, --help      Show program help message and exit")
    print("\n  Examples:")
    print("    downplay -u \"https://www.youtube.com/watch?v=dQw4w9WgXcQ\"")
    print("    downplay -u \"https://www.youtube.com/playlist?list=...\" -o MyDownloads")
    print("=" * 60)
    input("\n  Press Enter to return to the main menu...")


def interactive_mode():
    """Guided interactive menu mode."""
    while True:
        os.system('cls' if os.name == 'nt' else 'clear')
        has_ffmpeg, _ = check_ffmpeg()
        ffmpeg_status = "Installed" if has_ffmpeg else "Missing (Limited to 720p)"
        
        print("=" * 60)
        print("                    D O W N P L A Y")
        print("=" * 60)
        print("  YouTube Video & Playlist Downloader CLI")
        print("  Version 1.0.0 | By SoufianoDev")
        print("=" * 60)
        print(f"  FFmpeg Dependency Status: {ffmpeg_status}")
        print("=" * 60)
        print("\n  [1] Download a Video or Playlist (Guided)")
        print("  [2] Manage FFmpeg Dependency")
        print("  [3] Help & CLI Usage Info")
        print("  [4] Exit")
        
        choice = input("\n  Select an option [1-4]: ").strip()
        
        if choice == '1':
            run_interactive_download()
        elif choice == '2':
            run_ffmpeg_manager()
        elif choice == '3':
            run_interactive_help()
        elif choice == '4':
            print("\n  Goodbye!")
            break
        else:
            input("\n  Invalid selection. Press Enter to try again...")


def main():
    """Main entry point."""
    # If launched with zero arguments, switch into guided interactive menu
    if len(sys.argv) == 1:
        interactive_mode()
        sys.exit(0)
        
    args = parse_args()
    
    if not args.url:
        print("Error: -u/--url is required when running in command-line mode.", file=sys.stderr)
        print("Use DownPlay without arguments to launch the interactive menu.", file=sys.stderr)
        sys.exit(1)
        
    print(f"{APP_NAME} by {APP_AUTHOR}")
    print(f"Version: {APP_VERSION}")
    print(f"URL: {args.url}")
    print(f"Output: {os.path.abspath(args.output)}")
    
    # CLI Dynamic FFmpeg warning check
    has_ffmpeg, _ = check_ffmpeg()
    if not has_ffmpeg:
        print("Warning: FFmpeg not detected. Falling back to progressive downloads (max 720p).", file=sys.stderr)
        
    if args.mode == 'auto':
        mode = detect_mode(args.url)
        print(f"Mode: auto-detected as '{mode}'")
    else:
        mode = args.mode
        print(f"Mode: {mode} (user-specified)")
        
    tracker = {'success': 0, 'fail': 0}
    options = build_ytdlp_options(args, mode, tracker)
    
    if has_ffmpeg:
        print("\nFormat: Best available quality streams (dynamic merging)")
    else:
        print("\nFormat: Progressive only [vcodec+acodec] (no merging)")
        
    print("Starting download...\n")
    
    try:
        download(args.url, options)
    except KeyboardInterrupt:
        print("\n⚠ Interrupted")
        
    total = tracker['success'] + tracker['fail']
    print(f"\n{'='*60}")
    print("DOWNLOAD SUMMARY")
    print(f"{'='*60}")
    print(f"  Successful downloads: {tracker['success']}")
    print(f"  Failed downloads:     {tracker['fail']}")
    print(f"  Total items:          {total}")
    print(f"{'='*60}")
    
    if tracker['fail'] > 0:
        print(f"\n⚠ {tracker['fail']} item(s) failed to download")
        sys.exit(1)
    elif total == 0:
        print(f"\n⚠ No items were downloaded")
        sys.exit(1)
    else:
        print(f"\n✓ All downloads completed successfully")
        sys.exit(0)


if __name__ == '__main__':
    main()
