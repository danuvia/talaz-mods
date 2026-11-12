# Talaz Mods

The mods of [Talaz](https://github.com/danuvia/talaz), a Minecraft Bedrock client by Danuvia. They're one library, `libtalaz_bedrock.so`, which mcpelauncher loads next to the game. In a world, Right Shift opens the menu.

## Layout

`src/mods.def` lists every mod and its settings once. The game menu builds its tiles and pages from it, and the launcher reads it for its Mods page.

`src/mods/` has one file per mod. `src/core/` holds what they share: the menu, the title screen, the settings file, the tools for finding and hooking things in the game, the world pass the visual mods draw in, and the input the mods watch.

`pack/` is the resource pack the launcher puts in the game folder for the title screen.

To add a mod, write a `MOD` line in `src/mods.def` with its settings below it, and a file in `src/mods/`. CMake picks the file up by itself. The hooks into the frame are declared in `src/core/shared.h`.

## Building

You need CMake and the Android NDK.

```bash
cmake -S . -B build/arm64 -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build/arm64 -j8
```

On Linux use `-DANDROID_ABI=x86_64` and `build/x86_64`. The launcher carries the library inside itself.

A few variables in the game's environment help while working on the mods. `TALAZ_PREVIEW=1` opens the menu at startup and `TALAZ_PREVIEW=zoom` opens a mod's page. `TALAZ_BENCH=1` logs the frame rate every second, and `TALAZ_BENCH_ONLY=fps,riptide` runs only those mods. `TALAZ_AB=1` turns the new parts of Riptide off for 3 seconds out of every 6, so one run compares both.

## License

GPL-3.0. See [ATTRIBUTION.md](ATTRIBUTION.md) for the work the mods build on.
