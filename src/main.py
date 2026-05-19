#!/usr/bin/env python3
"""
YouTube Playlist Downloader CLI 
Downloads videos and playlists in progressive formats only (no merging).
"""

import argparse
import os
import sys

APP_NAME = "DownPlay"
APP_VERSION = "1.0.0"
APP_AUTHOR = "SoufianoDev"


def get_ytdlp():
    """Import yt_dlp only when the app actually needs it."""
    try:
        import yt_dlp
        return yt_dlp
    except ModuleNotFoundError:
        return None


def parse_args():
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description='Download YouTube videos and playlists using yt-dlp',
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        '-u', '--url',
        required=True,
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
        # Default to video if detection fails
        return 'video'


def create_progress_hook(tracker):
    """Factory function to create a progress hook with access to tracker."""
    def progress_hook(d):
        if d['status'] == 'downloading':
            # Calculate percentage
            total = d.get('total_bytes') or d.get('total_bytes_estimate')
            if total and total > 0:
                percent = min(100.0, d['downloaded_bytes'] / total * 100)
            else:
                percent = 0.0
            
            # Format speed
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
            
            # Format ETA
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
    """Build yt-dlp options dictionary based on arguments and mode."""
    # Ensure output directory exists
    os.makedirs(args.output, exist_ok=True)
    
    # Set output template based on mode
    if mode == 'playlist':
        # Use playlist indexing for filenames: "01 - Title.ext"
        outtmpl = os.path.join(args.output, '%(playlist_index)02d - %(title)s.%(ext)s')
    else:
        # Single video: "Title.ext"
        outtmpl = os.path.join(args.output, '%(title)s.%(ext)s')
    
    # Format selector: ONLY progressive formats (video+audio in one file)
    # [vcodec!=none][acodec!=none] ensures both codecs exist in same container
    # No fallback - fails safely if only separate streams available
    format_selector = 'best[vcodec!=none][acodec!=none]'
    
    options = {
        'format': format_selector,
        'outtmpl': outtmpl,
        'progress_hooks': [create_progress_hook(tracker)],
        'no_post_overwrites': True,
        'ignoreerrors': True,  # Continue downloading playlist even if some items fail
        'quiet': False,
        'no_warnings': False,
        # CRITICAL: Prevent any ffmpeg usage for merging/post-processing
        'ffmpeg_location': None,
        # Disable all postprocessors to avoid external tool dependencies
        'postprocessors': [],
        # Explicitly disable format merging
        'merge_output_format': None,
    }
    
    # Configure playlist handling based on mode
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


def main():
    """Main entry point."""
    args = parse_args()
    
    print(f"{APP_NAME} by {APP_AUTHOR}")
    print(f"Version: {APP_VERSION}")
    print(f"URL: {args.url}")
    print(f"Output: {os.path.abspath(args.output)}")
    
    # Determine download mode
    if args.mode == 'auto':
        mode = detect_mode(args.url)
        print(f"Mode: auto-detected as '{mode}'")
    else:
        mode = args.mode
        print(f"Mode: {mode} (user-specified)")
    
    # Tracker for success/failure counts
    tracker = {'success': 0, 'fail': 0}
    
    # Build yt-dlp options
    options = build_ytdlp_options(args, mode, tracker)
    
    print(f"\nFormat: Progressive only [vcodec+acodec] (no merging)")
    print(f"Starting download...\n")
    
    # Execute download
    try:
        download(args.url, options)
    except KeyboardInterrupt:
        print("\n⚠ Interrupted")
    
    # Print final summary
    total = tracker['success'] + tracker['fail']
    print(f"\n{'='*60}")
    print(f"DOWNLOAD SUMMARY")
    print(f"{'='*60}")
    print(f"  Successful downloads: {tracker['success']}")
    print(f"  Failed downloads:     {tracker['fail']}")
    print(f"  Total items:          {total}")
    print(f"{'='*60}")
    
    # Exit code based on failures
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
