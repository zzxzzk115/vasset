# vasset

<h4 align="center">
  vasset is my general asset pipeline tool on top of Assimp &amp; other libraries.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/vasset/releases/latest" alt="Latest Release">
        <img src="https://img.shields.io/github/release/zzxzzk115/vasset?include_prereleases=&sort=semver&color=blue" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-Linux">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_linux.yaml?branch=master&label=Build-Linux&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-macOS">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_macos.yaml?branch=master&label=Build-macOS&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-Android">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_android.yaml?branch=master&label=Build-Android&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-WASM">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_wasm.yaml?branch=master&label=Build-WASM&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/vasset"></a>
    <a href="https://www.codefactor.io/repository/github/zzxzzk115/vasset"><img src="https://www.codefactor.io/repository/github/zzxzzk115/vasset/badge" alt="CodeFactor" /></a>
    <a href="https://github.com/zzxzzk115/vasset/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/vasset"></a>
</p>

## Features

- Custom asset file formats: VMesh, VTexture, VMaterial
- Asset importers: VMeshImporter, VTextureImporter, VAssetImporter
- Asset registry: Manage assets with UUIDs
- Asset packer: Pack assets into a single VPK file
- VPK filesystem: Mount VPK file with Virtual File System (from [vfilesystem](https://github.com/zzxzzk115/vfilesystem)) and load assets with URI based string
- Library & CLI: `vasset` and `vasset-cli` can be integrated into engines
- WASM import support: `vasset-import` can now be built for `wasm`, including the browser upload demo in `examples/import_web`

## Prebuilt Releases

Tagged releases publish prebuilt bundles on [GitHub Releases](https://github.com/zzxzzk115/vasset/releases).

- Desktop bundles currently stage `vbase`, `vfilesystem`, `vasset`, `vasset-import`, and `vasset-cli`
- Android bundles currently stage the runtime stack: `vbase`, `vfilesystem`, and `vasset`
- WASM source builds include `vasset-import` by default; pass `--vasset_enable_wasm_import=n` for a runtime-only build
- `vasset-cli` remains desktop-only; the WASM path currently targets library usage and the browser upload example
- These bundles are project-focused prebuilds rather than a fully vendored SDK, so compile-time third-party dependencies such as `glm` and `assimp` are still expected to come from your own toolchain or package manager

## CLI Usage

```
./vasset-cli import <asset-root>
./vasset-cli pack <asset-root> <out.vpk> [--zstd <zstd-level>]
```

## VPK Loading Example

```cpp
#include <vasset/vasset.hpp>
#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <iostream>

using namespace vasset;

int main()
{
    auto vpkFS = std::make_shared<VpkFileSystem>("out.vpk");
    vpkFS->openPackage();

    vfilesystem::VirtualFileSystem vfs {};
    vfs.mount(vpkFS, "res");

    // Load mesh
    auto r = vfs.open("res://models/DamagedHelmet/DamagedHelmet.gltf", vfilesystem::FileMode::eRead);

    auto vpkMemFile = std::move(r.value());

    VMesh mesh {};
    auto  res = loadMeshFromMemory(vpkMemFile->readAllBytes(), mesh);

    std::cout << "Loaded mesh (res://models/DamagedHelmet/DamagedHelmet.gltf) from VPK: " << mesh.name << " with " << mesh.vertexCount << " vertices." << std::endl;

    // Load texture
    r = vfs.open("res://textures/awesomeface.png", vfilesystem::FileMode::eRead);

    vpkMemFile = std::move(r.value());

    VTexture texture {};
    res = loadTextureFromMemory(vpkMemFile->readAllBytes(), texture);

    std::cout << "Loaded texture (res://textures/awesomeface.png) from VPK: " << " (" << texture.width << "x" << texture.height << ")" << std::endl;

    return 0;
}
```

## Build Instructions

Prerequisites:

- Git
- XMake
- Visual Studio with MSVC if Windows
- GCC or Clang if Linux/Unix
- XCode with GCC or Apple Clang if macOS

Step-by-Step:

- Install XMake by following [this](https://xmake.io/guide/quick-start.html#installation).

- Clone the project:

  ```bash
  git clone --recursive https://github.com/zzxzzk115/vasset.git
  ```

- Build the project:

  ```bash
  cd vasset
  git submodule update --init --recursive
  xmake -vD
  ```

- Run the tests:

  ```bash
  xmake run test-binary-serialization
  xmake run test-importers
  ```

- Run the CLI:

  ```bash
  xmake run vasset-cli import <asset-root>
  # example
  xmake run vasset-cli import /path/to/resources
  ```

  ```bash
  xmake run vasset-cli pack <asset-root>  <out.vpk> [--zstd <zstd-level>]
  # example
  xmake run vasset-cli pack /path/to/resources /path/to/resources.vpk --zstd 6
  ```

## WASM Import Build

`vasset-import` now supports WASM builds and is enabled by default.

Build the browser import demo:

```bash
xmake f -p wasm --vasset_build_examples=y
xmake build vasset-example-import-web
```

Notes:

- The demo imports through the browser-facing Emscripten filesystem, not through native host paths
- The generated output is `build/wasm/wasm32/<mode>/vasset-example-import-web/`
- Serve that directory over HTTP and open `vasset-example-import-web.html`
- `Add folder` preserves package-relative paths for models with external `.bin` and texture dependencies
- Single-file `.gltf` selection is also supported in modern browsers by requesting the containing directory when external dependencies need to be resolved

## License

This project is under the [MIT](LICENSE) license.
