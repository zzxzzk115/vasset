# vasset

My general asset pipeline tool on top of Assimp &amp; other libraries.

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
- Vulkan SDK
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
  xmake run vasset-cli <asset-folder> <imported-folder> [working-directory]
  # example
  xmake run vasset-cli resources imported <path-to-vasset-project-root>
  # example for Powershell on Windows
  xmake run vasset-cli resources imported $pwd
  ```

## License

This project is under the [MIT](LICENSE) license.
