ImGuiPlayer README
====================

ImGuiPlayer is a simple application for playing video files with Dear ImGui.

## Build

```
git clone git@github.com:MonocleSecurity/ImGuiPlayer.git --recursive
cd ImGuiPlayer
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake -G"Unix Makefiles" .
make
```

## Run

./ImGuiPlayer video.mp4
