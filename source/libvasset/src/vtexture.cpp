#include "vasset/vtexture.hpp"
#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>

#include <filesystem>
#include <fstream>

namespace vasset
{
    vbase::Result<void, AssetError> saveTexture(const VTexture& texture, vbase::StringView filePath)
    {
        // Binary writing
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());
        std::ofstream file(std::string(filePath), std::ios::binary);
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

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

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> loadTexture(vbase::StringView filePath, VTexture& outTexture)
    {
        std::filesystem::path path(filePath);

        if (!std::filesystem::exists(path))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        const auto size = std::filesystem::file_size(path);
        if (size == 0)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        std::vector<std::byte> buffer(size);

        std::ifstream file(path, std::ios::binary);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));

        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        return loadTextureFromMemory(buffer, outTexture);
    }

    vbase::Result<void, AssetError> loadTextureFromMemory(const std::vector<std::byte>& data, VTexture& outTexture)
    {
        if (data.size() < 16 + 16 + 4 * 9) // minimum size check (magic + uuid + 9 uint32 fields)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        size_t offset = 0;

        auto readSafe = [&](void* dst, size_t size) -> bool {
            if (offset + size > data.size())
                return false;

            std::memcpy(dst, data.data() + offset, size);
            offset += size;
            return true;
        };

        // 16 bytes for magic number
        char magic[16];
        readSafe(magic, sizeof(magic));
        if (std::string(magic) != "VTEXTURE")
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        // 16 bytes for UUID
        readSafe(&outTexture.uuid, sizeof(outTexture.uuid));

        // 4 bytes for width
        readSafe(&outTexture.width, sizeof(outTexture.width));

        // 4 bytes for height
        readSafe(&outTexture.height, sizeof(outTexture.height));

        // 4 bytes for depth
        readSafe(&outTexture.depth, sizeof(outTexture.depth));

        // 4 bytes for mip levels
        readSafe(&outTexture.mipLevels, sizeof(outTexture.mipLevels));

        // 4 bytes for array layers
        readSafe(&outTexture.arrayLayers, sizeof(outTexture.arrayLayers));

        // 4 bytes for isCubemap
        readSafe(&outTexture.isCubemap, sizeof(outTexture.isCubemap));

        // 4 bytes for generateMipmaps
        readSafe(&outTexture.generateMipmaps, sizeof(outTexture.generateMipmaps));

        // 4 bytes for type
        readSafe(&outTexture.type, sizeof(outTexture.type));

        // 4 bytes for format
        readSafe(&outTexture.format, sizeof(outTexture.format));

        // 4 bytes for file format
        readSafe(&outTexture.fileFormat, sizeof(outTexture.fileFormat));

        // 4 bytes for data size
        uint32_t dataSize = 0;
        readSafe(&dataSize, sizeof(dataSize));

        if (offset + dataSize > data.size())
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        // N bytes for data
        outTexture.data.resize(dataSize);
        std::memcpy(outTexture.data.data(), data.data() + offset, dataSize);
        offset += dataSize;

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset