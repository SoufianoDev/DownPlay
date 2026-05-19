# py2c targets

## assembleRelease

`./py2c assembleRelease` builds the production artifact at `release/downplay`.
On Windows, use `py2c.bat assembleRelease`.
On Windows, the final release artifact is also a single `downplay.exe`.

Runtime contract:

- End users run the single executable `release/downplay`.
- No Python installation is required on the target machine.
- No pip install step is required on the target machine.
- Runtime imports, the Python stdlib, native extension modules, site-packages, and `libpython` are embedded into the executable payload.
- The executable extracts its private runtime internally before handing off to the Cython-built binary.
- Python user site-packages are disabled with `PYTHONNOUSERSITE=1`.
- Build flags prefer optimized native output.
- py2c/Cython/Python internals are hidden from normal runtime output.

For Linux, `release/downplay` is the only file the end user needs.

## assembleDebug

`./py2c assembleDebug` builds the development artifact at `build/downplay`.
On Windows, use `py2c.bat assembleDebug`.

Runtime contract:

- Python remains visible and active.
- The binary uses live project source and `.build-venv/site-packages`.
- py2c boundary diagnostics are printed to stderr.
- `PYTHONFAULTHANDLER=1`, `PYTHONUNBUFFERED=1`, and `PYTHONTRACEMALLOC=1` are enabled.
- Build flags prefer debug symbols and no optimization.
- Stack traces and Python import/runtime behavior are intentionally exposed.

`./py2c build` is an alias for `assembleDebug`.

## dependency bootstrap

Use `./py2c installDeps` to initialize `.build-venv` and install or refresh build/runtime dependencies.
On Windows, use `py2c.bat installDeps`.

Use `--auto-install` before the command to force dependency refresh during a build:

```sh
./py2c --auto-install assembleDebug
./py2c --auto-install assembleRelease
```

Windows:

```bat
py2c.bat --auto-install assembleDebug
py2c.bat --auto-install assembleRelease
```

Dependency sources:

- `cython` is installed as a build dependency.
- `requirements.txt` is installed for runtime dependencies.
- `requirements-build.txt` is installed when present for extra build-only dependencies.
