#include "vasset/vtexture.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace vasset
{
    constexpr const char* META_FILE_EXTENSION = ".vmeta";

    bool saveTexture(const VTexture& texture, vbase::StringView filePath, vbase::StringView srcFilePath)
    {
        // Binary writing
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());
        std::ofstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        const char magic[16] = "VTEXTURE\0";
        file.write(magic, sizeof(magic));

        // 16 bytes for UUID
        file.write(reinterpret_cast<const char*>(&texture.uuid), sizeof(texture.uuid));

        // 4 bytes for width
        file.write(reinterpret_cast<const char*>(&texture.width), sizeof(texture.width));

        // 4 bytes for height
        file.write(reinterpret_cast<const char*>(&texture.height), sizeof(texture.height));

        // 4 bytes for depth
        file.write(reinterpret_cast<const char*>(&texture.depth), sizeof(texture.depth));

        // 4 bytes for mip levels
        file.write(reinterpret_cast<const char*>(&texture.mipLevels), sizeof(texture.mipLevels));

        // 4 bytes for array layers
        file.write(reinterpret_cast<const char*>(&texture.arrayLayers), sizeof(texture.arrayLayers));

        // 4 bytes for isCubemap
        file.write(reinterpret_cast<const char*>(&texture.isCubemap), sizeof(texture.isCubemap));

        // 4 bytes for generateMipmaps
        file.write(reinterpret_cast<const char*>(&texture.generateMipmaps), sizeof(texture.generateMipmaps));

        // 4 bytes for type
        file.write(reinterpret_cast<const char*>(&texture.type), sizeof(texture.type));

        // 4 bytes for format
        file.write(reinterpret_cast<const char*>(&texture.format), sizeof(texture.format));

        // 4 bytes for file format
        file.write(reinterpret_cast<const char*>(&texture.fileFormat), sizeof(texture.fileFormat));

        // 4 bytes for data size
        uint32_t dataSize = static_cast<uint32_t>(texture.data.size());
        file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));

        // N bytes for data
        file.write(reinterpret_cast<const char*>(texture.data.data()), dataSize);

        file.close();

        // Save meta file as json by nlohmann_json
        auto srcFileOSPath  = std::filesystem::path(srcFilePath);
        auto metaFileOSPath = srcFileOSPath;
        metaFileOSPath.replace_extension(META_FILE_EXTENSION);

        VTextureMeta meta {};
        meta.uuid      = texture.uuid;
        meta.extension = srcFileOSPath.extension().string();
        std::ofstream metaFile(metaFileOSPath);
        if (!metaFile)
            return false;
        metaFile << nlohmann::json {
            {"uuid", vbase::to_string(meta.uuid)},
            {"extension", meta.extension},
        };
        metaFile.close();

        return true;
    }

    bool loadTexture(vbase::StringView filePath, VTexture& outTexture)
    {
        // Binary reading
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        char magic[16];
        file.read(magic, sizeof(magic));
        if (std::string(magic) != "VTEXTURE")
            return false;

        // 16 bytes for UUID
        file.read(reinterpret_cast<char*>(&outTexture.uuid), sizeof(outTexture.uuid));

        // 4 bytes for width
        file.read(reinterpret_cast<char*>(&outTexture.width), sizeof(outTexture.width));

        // 4 bytes for height
        file.read(reinterpret_cast<char*>(&outTexture.height), sizeof(outTexture.height));

        // 4 bytes for depth
        file.read(reinterpret_cast<char*>(&outTexture.depth), sizeof(outTexture.depth));

        // 4 bytes for mip levels
        file.read(reinterpret_cast<char*>(&outTexture.mipLevels), sizeof(outTexture.mipLevels));

        // 4 bytes for array layers
        file.read(reinterpret_cast<char*>(&outTexture.arrayLayers), sizeof(outTexture.arrayLayers));

        // 4 bytes for isCubemap
        file.read(reinterpret_cast<char*>(&outTexture.isCubemap), sizeof(outTexture.isCubemap));

        // 4 bytes for generateMipmaps
        file.read(reinterpret_cast<char*>(&outTexture.generateMipmaps), sizeof(outTexture.generateMipmaps));

        // 4 bytes for type
        file.read(reinterpret_cast<char*>(&outTexture.type), sizeof(outTexture.type));

        // 4 bytes for format
        file.read(reinterpret_cast<char*>(&outTexture.format), sizeof(outTexture.format));

        // 4 bytes for file format
        file.read(reinterpret_cast<char*>(&outTexture.fileFormat), sizeof(outTexture.fileFormat));

        // 4 bytes for data size
        uint32_t dataSize = 0;
        file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));

        // N bytes for data
        outTexture.data.resize(dataSize);
        file.read(reinterpret_cast<char*>(outTexture.data.data()), dataSize);

        file.close();

        return true;
    }
} // namespace vasset