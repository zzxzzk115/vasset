# vasset

<h4 align="center">
  vasset is my general asset pipeline tool on top of Assimp &amp; other libraries.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-Linux">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_linux.yaml?branch=master&label=Build-Linux&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/actions" alt="Build-macOS">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/vasset/build_macos.yaml?branch=master&label=Build-macOS&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/vasset/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/vasset"></a>
    <a href="https://www.codefactor.io/repository/github/zzxzzk115/vasset"><img src="https://www.codefactor.io/repository/github/zzxzzk115/vasset/badge" alt="CodeFactor" /></a>
    <a href="https://github.com/zzxzzk115/vasset/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/vasset"></a>
</p>

## Features

- Custom asset file formats: .vmesh, .vmat, .vtex
- Asset importers: VMeshImporter, VTextureImporter, VAssetImporter
- Asset registry: Manage assets with UUIDs
- Library & CLI: `vasset` and `vasset-cli` can be integrated into engines

## CLI Usage

```bash
./vasset-cli <asset-folder> <imported-folder> [working-directory]
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
  git clone https://github.com/zzxzzk115/vasset.git
  ```

- Build the project:

  ```bash
  cd vasset
  xmake -vD
  ```

- Run the tests:

  ```bash
  xmake run test-binary-serialization
  xmake run test-importers
  ```

- Run the CLI:
  ```bash
  xmake run vasset-cli import <asset-folder> <imported-folder> [working-directory]
  # example
  xmake run vasset-cli import resources imported <path-to-vasset-project-root>
  # example for Powershell on Windows
  xmake run vasset-cli import resources imported $pwd
  ```

  ```bash
  xmake run vasset-cli pack <asset-root> <out.vpk>
  # example
  xmake run vasset-cli pack resources resources.vpk
  ```

## License

This project is under the [MIT](LICENSE) license.
