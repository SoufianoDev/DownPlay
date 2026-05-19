#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
RELEASE = ROOT / "release"
VENV = ROOT / ".build-venv"
SRC = ROOT / "src" / "main.py"
APP = "downplay"


def log(message: str) -> None:
    print(f"[*] {message}")


def fail(message: str) -> None:
    print(f"[!] {message}", file=sys.stderr)
    raise SystemExit(1)


def run(cmd: list[str], *, verbose: bool = False) -> None:
    if verbose:
        print("$", " ".join(str(part) for part in cmd))
    subprocess.run(cmd, check=True)


def venv_path(*parts: str) -> Path:
    return VENV / "Scripts" / Path(*parts)


def ensure_venv(auto_install: bool, verbose: bool) -> None:
    if not VENV.exists():
        log("Creating virtual environment")
        venv.EnvBuilder(with_pip=True).create(VENV)

    py = venv_path("python.exe")
    cython = venv_path("cython.exe")
    should_install = auto_install or not cython.exists()
    if not should_install:
        return

    log("Installing build/runtime dependencies")
    run([str(py), "-m", "pip", "install", "--upgrade", "pip", "wheel", "setuptools"], verbose=verbose)
    run([str(py), "-m", "pip", "install", "--upgrade", "cython"], verbose=verbose)

    requirements = ROOT / "requirements.txt"
    if requirements.exists():
        run([str(py), "-m", "pip", "install", "--upgrade", "-r", str(requirements)], verbose=verbose)
    else:
        run([str(py), "-m", "pip", "install", "--upgrade", "yt-dlp"], verbose=verbose)

    build_requirements = ROOT / "requirements-build.txt"
    if build_requirements.exists():
        run([str(py), "-m", "pip", "install", "--upgrade", "-r", str(build_requirements)], verbose=verbose)


def python_version() -> str:
    return f"{sys.version_info.major}.{sys.version_info.minor}"


def site_packages() -> str:
    py = venv_path("python.exe")
    out = subprocess.check_output(
        [str(py), "-c", "import site; print(site.getsitepackages()[0])"],
        text=True,
    )
    return out.strip()


def cython_embed(verbose: bool, mode: str) -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    cython = venv_path("cython.exe")
    main_c = BUILD / "main.c"
    run([str(cython), str(SRC), "--embed", "-o", str(main_c)], verbose=verbose)

    text = main_c.read_text(encoding="utf-8")
    text = text.replace("int\nmain(", "int\n__real_main(")
    text = text.replace("int main(", "int __real_main(")
    text = text.replace("int wmain(", "int __real_main(")
    main_c.write_text(text, encoding="utf-8")

    wrapper = BUILD / "wrapper.c"
    pyver = python_version()
    sp = site_packages().replace("\\", "\\\\")
    root = str(ROOT).replace("\\", "\\\\")
    wrapper.write_text(make_wrapper(mode, pyver, sp, root), encoding="utf-8")


def make_wrapper(mode: str, pyver: str, sitepackages_value: str, root_value: str) -> str:
    if mode == "release":
        runtime_setup = f"""
  _putenv_s("PYTHONNOUSERSITE", "1");
  snprintf(path, sizeof(path), "%s\\\\runtime", dir);
  _putenv_s("PYTHONHOME", path);
  snprintf(path, sizeof(path),
    "%s\\\\runtime\\\\Lib;%s\\\\runtime\\\\DLLs;%s\\\\runtime\\\\Lib\\\\site-packages",
    dir, dir, dir);
  _putenv_s("PYTHONPATH", path);
"""
    else:
        runtime_setup = f"""
  _putenv_s("DOWNPLAY_DEBUG", "1");
  _putenv_s("PYTHONFAULTHANDLER", "1");
  _putenv_s("PYTHONUNBUFFERED", "1");
  _putenv_s("PYTHONTRACEMALLOC", "1");
  snprintf(path, sizeof(path), "{root_value}\\\\src;{sitepackages_value}");
  _putenv_s("PYTHONPATH", path);
  fprintf(stderr, "[py2c-debug] exe=%s\\n", argv[0]);
  fprintf(stderr, "[py2c-debug] PYTHONPATH=%s\\n", path);
"""
    return f"""#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int __real_main(int argc, char **argv);

static void exe_dir(char *out, size_t size, char **argv) {{
  DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size - 1);
  if (n == 0) {{
    strncpy(out, argv[0], size - 1);
    out[size - 1] = '\\0';
  }} else {{
    out[n] = '\\0';
  }}
  char *slash = strrchr(out, '\\\\');
  if (!slash) slash = strrchr(out, '/');
  if (slash) *slash = '\\0';
  else strcpy(out, ".");
}}

int main(int argc, char **argv) {{
  char dir[4096];
  char path[8192];
  exe_dir(dir, sizeof(dir), argv);
{runtime_setup}
  return __real_main(argc, argv);
}}
"""


def compile_binary(mode: str, verbose: bool) -> Path:
    output_dir = (BUILD / "release-payload") if mode == "release" else BUILD
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / (f"{APP}-bin.exe" if mode == "release" else f"{APP}.exe")
    include = sysconfig.get_path("include")
    libs = Path(sys.base_prefix) / "libs"
    py_lib = f"python{sys.version_info.major}{sys.version_info.minor}"
    main_c = BUILD / "main.c"
    wrapper_c = BUILD / "wrapper.c"

    cl = shutil.which("cl")
    gcc = shutil.which("gcc")
    if cl:
        flags = ["/O2", "/DNDEBUG"] if mode == "release" else ["/Od", "/Zi", "/DDEBUG"]
        cmd = [
            "cl",
            "/nologo",
            *flags,
            str(main_c),
            str(wrapper_c),
            f"/I{include}",
            "/link",
            f"/LIBPATH:{libs}",
            f"{py_lib}.lib",
            f"/OUT:{output}",
        ]
    elif gcc:
        flags = ["-O3", "-DNDEBUG"] if mode == "release" else ["-O0", "-g3", "-DDEBUG"]
        cmd = [
            "gcc",
            str(main_c),
            str(wrapper_c),
            *flags,
            f"-I{include}",
            f"-L{libs}",
            f"-l{py_lib}",
            "-o",
            str(output),
        ]
    else:
        fail("No C compiler found. Install Visual Studio Build Tools or MinGW-w64.")

    run(cmd, verbose=verbose)
    return output


def copytree(src: Path, dst: Path) -> None:
    if src.exists():
        shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))


def bundle_release_runtime(payload_dir: Path) -> None:
    runtime = payload_dir / "runtime"
    lib_dst = runtime / "Lib"
    site_dst = lib_dst / "site-packages"
    dll_dst = runtime / "DLLs"
    runtime.mkdir(parents=True, exist_ok=True)
    lib_dst.mkdir(parents=True, exist_ok=True)

    stdlib = Path(sysconfig.get_path("stdlib"))
    for item in stdlib.iterdir():
        if item.name in {"__pycache__", "site-packages", "dist-packages", "ensurepip", "idlelib", "tkinter", "turtledemo", "test"}:
            continue
        target = lib_dst / item.name
        if item.is_dir():
            copytree(item, target)
        elif item.suffix in {".py", ".txt", ".pem"}:
            shutil.copy2(item, target)

    copytree(Path(sys.base_prefix) / "DLLs", dll_dst)
    copytree(Path(site_packages()), site_dst)

    dll_name = f"python{sys.version_info.major}{sys.version_info.minor}.dll"
    candidates = [
        Path(sys.base_prefix) / dll_name,
        Path(sys.base_prefix) / "DLLs" / dll_name,
        Path(os.environ.get("WINDIR", "C:\\Windows")) / "System32" / dll_name,
    ]
    for candidate in candidates:
        if candidate.exists():
            shutil.copy2(candidate, payload_dir / dll_name)
            return
    fail(f"Could not find {dll_name} to bundle")


def build_onefile_release(payload_dir: Path, verbose: bool) -> Path:
    launcher_src = ROOT / "src" / "wrapper" / "onefile_launcher_windows.c"
    launcher_obj = BUILD / "downplay-launcher.exe"
    final_output = RELEASE / f"{APP}.exe"
    tar_path = BUILD / "payload.tar"

    if not launcher_src.exists():
        fail(f"Missing launcher source: {launcher_src}")

    include = sysconfig.get_path("include")
    libs = Path(sys.base_prefix) / "libs"
    py_lib = f"python{sys.version_info.major}{sys.version_info.minor}"
    cl = shutil.which("cl")
    gcc = shutil.which("gcc")
    if cl:
        cmd = [
            "cl",
            "/nologo",
            "/O2",
            "/DNDEBUG",
            "/municode",
            str(launcher_src),
            f"/I{include}",
            "/link",
            f"/LIBPATH:{libs}",
            f"{py_lib}.lib",
            f"/OUT:{launcher_obj}",
        ]
    elif gcc:
        cmd = [
            "gcc",
            str(launcher_src),
            "-O2",
            "-DNDEBUG",
            "-municode",
            f"-I{include}",
            f"-L{libs}",
            f"-l{py_lib}",
            "-o",
            str(launcher_obj),
        ]
    else:
        fail("No C compiler found for Windows launcher build.")

    run(cmd, verbose=verbose)

    with tarfile.open(tar_path, "w", format=tarfile.GNU_FORMAT) as tar:
        tar.add(payload_dir, arcname=".")

    marker = b"\n__DOWNPLAY_PAYLOAD_V1__\n"
    final_output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(launcher_obj, final_output)
    with final_output.open("ab") as out, tar_path.open("rb") as archive:
        out.write(marker)
        shutil.copyfileobj(archive, out)
    return final_output


def clean() -> None:
    for path in (BUILD, RELEASE, VENV):
        if path.exists():
            shutil.rmtree(path)


def verify() -> None:
    missing = [path for path in (SRC, ROOT / "py2c.bat", ROOT / "py2c_windows.py", ROOT / "src" / "wrapper" / "onefile_launcher_windows.c") if not path.exists()]
    if missing:
        for path in missing:
            print(f"[FAIL] {path}")
        raise SystemExit(1)
    print("[PASS] Windows py2c files")


def main() -> None:
    args = sys.argv[1:]
    verbose = False
    auto_install = False
    while args and args[0] in {"--verbose", "-v", "--auto-install"}:
        token = args.pop(0)
        if token in {"--verbose", "-v"}:
            verbose = True
        elif token == "--auto-install":
            auto_install = True

    command = args.pop(0) if args else "assembleDebug"
    if command in {"--help", "-h"}:
        print("Usage: py2c.bat [--auto-install] [--verbose] <setup|installDeps|assembleDebug|assembleRelease|build|clean|verify>")
        return
    if command == "clean":
        clean()
        return
    if command == "verify":
        verify()
        return
    if command in {"setup", "installDeps"}:
        ensure_venv(True, verbose)
        return
    if command == "build":
        command = "assembleDebug"
    if command not in {"assembleDebug", "assembleRelease"}:
        fail(f"Unknown command: {command}")

    mode = "release" if command == "assembleRelease" else "debug"
    ensure_venv(auto_install, verbose)
    cython_embed(verbose, mode)
    output = compile_binary(mode, verbose)
    if mode == "release":
        payload_dir = BUILD / "release-payload"
        bundle_release_runtime(payload_dir)
        output = build_onefile_release(payload_dir, verbose)
    log(f"Binary: {output}")


if __name__ == "__main__":
    main()
