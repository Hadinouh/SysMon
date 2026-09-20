# SysMon v0.9.0 Git release steps

Run these commands from your local SysMon repository after replacing/updating the source files with this release-ready set.

## 1. Make sure you are on v0.9-dev

```powershell
git switch v0.9-dev
git pull
```

## 2. Remove accidental generated/vendor-only changes

If you did not intentionally edit LibreHardwareMonitor itself, restore it before staging. This avoids committing line-ending-only changes:

```powershell
git restore -- SysMonSensors/ThirdParty
```

Generated files such as `SysMon.res`, `SysMon.exe`, test EXEs, `dist/`, and `SysMon.ini` are ignored by `.gitignore`.

## 3. Review exactly what will be committed

```powershell
git status
git diff --stat
git diff
```

## 4. Build and test

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -RunTests
```

Close any running `SysMon.exe` first if the build reports `Permission denied`.

## 5. Stage and commit v0.9.0

```powershell
git add .
git status
git commit -m "Release v0.9.0 monitoring, settings and UI overhaul"
```

## 6. Push the development branch

```powershell
git push origin v0.9-dev
```

## 7. Merge into main

```powershell
git switch main
git pull
git merge v0.9-dev
git push origin main
```

## 8. Tag the release

```powershell
git tag -a v0.9.0 -m "SysMon v0.9.0"
git push origin v0.9.0
```

## 9. Build the downloadable release ZIP

```powershell
powershell -ExecutionPolicy Bypass -File .\package-release.ps1 -RunTests
```

The finished file will be created in:

```text
dist\SysMon-v0.9.0-Windows-x64.zip
```

Create a GitHub Release from tag `v0.9.0`, paste the contents of `RELEASE-NOTES-v0.9.0.md`, and upload the ZIP from `dist`.
