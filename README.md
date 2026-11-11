# Talaz Mods

The mods of [Talaz](https://github.com/danuvia/talaz), a Minecraft Bedrock client by Danuvia. They are one library, `libtalaz_bedrock.so`, which mcpelauncher loads next to the game. Right Shift opens the menu in a world.

## Layout

- `src/mods.def` lists every mod and its settings, once. The game menu builds its tiles and pages from it, and so does the launcher.
- `src/mods/` holds the mods, one file each.
- `src/core/` is what they share: the menu, the title screen, the settings file, finding and hooking things in the game, the world pass the visual mods draw in, and the input the mods watch.
- `pack/` is the resource pack the launcher puts in the game folder for the title screen.

A new mod is one `MOD` line in `src/mods.def`, its settings below it, and a file in `src/mods/`. CMake picks the file up by itself; hook it into the frame through `src/core/shared.h`.

## Building

You need CMake and the Android NDK.

```bash
cmake -S . -B build/arm64 -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build/arm64 -j8
```

On Linux use `-DANDROID_ABI=x86_64` and `build/x86_64`. The launcher carries the library inside itself.

`TALAZ_PREVIEW=1` in the game's environment opens the menu at startup, `TALAZ_PREVIEW=zoom` a mod's page. `TALAZ_BENCH=1` logs the frame rate every second, and `TALAZ_BENCH_ONLY=fps,riptide` limits a run to those mods.

## License

GPL-3.0. See [ATTRIBUTION.md](ATTRIBUTION.md) for the work the mods build on.
