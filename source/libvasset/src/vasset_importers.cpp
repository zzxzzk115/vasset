#include "vasset/vasset_importers.hpp"

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>
#include <fstream>

namespace vasset
{
    VTextureImporter& VTextureImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    bool VTextureImporter::importTexture(const std::string& filePath, VTexture& outTexture) const
    {
        // Check path
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return false;

        // Check extension
        std::string ext = osPath.extension().string();
        if (ext == ".ktx2")
        {
            // Read KTX2 file
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
                return false;

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<char> bytes(size);
            if (!file.read(bytes.data(), size))
                return false;

            // Validate KTX2
            ktxTexture2* kTexture = nullptr;
            if (ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
                                             size,
                                             KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                             &kTexture) != KTX_SUCCESS)
            {
                return false;
            }

            int          width      = kTexture->baseWidth;
            int          height     = kTexture->baseHeight;
            int          depth      = kTexture->baseDepth;
            int          levels     = kTexture->numLevels;
            int          layers     = kTexture->numLayers;
            bool         isCubemap  = kTexture->numFaces == 6;
            bool         genMips    = kTexture->generateMipmaps;
            int          dimensions = kTexture->numDimensions;
            ktx_uint32_t format     = kTexture->vkFormat;

            // Fill outTexture
            outTexture.uuid            = VUUID::generate();
            outTexture.width           = width;
            outTexture.height          = height;
            outTexture.depth           = depth;
            outTexture.mipLevels       = levels;
            outTexture.arrayLayers     = layers;
            outTexture.isCubemap       = isCubemap;
            outTexture.generateMipmaps = genMips;
            outTexture.type            = static_cast<VTextureDimension>(dimensions);
            outTexture.format          = static_cast<VTextureFormat>(format);
            outTexture.data.assign(bytes.begin(), bytes.end());
        }
        else
        {
            // Load image file
            auto* file = stbi__fopen(filePath.c_str(), "rb");
            if (!file)
            {
                return false;
            }

            const auto hdr = stbi_is_hdr_from_file(file);

            // Load as raw image
            int width, height, channels;

            stbi_set_flip_vertically_on_load(m_Options.flipY ? 1 : 0);

            void* pixels = nullptr;
            if (hdr)
            {
                pixels = stbi_loadf_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
            }
            else
            {
                pixels = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
            }
            fclose(file);
            if (!pixels)
                return false;

            // Load other image formats
            if (m_Options.targetTextureFileFormat != VTextureFileFormat::eKTX2)
            {
                // Fill outTexture
                outTexture.uuid            = VUUID::generate();
                outTexture.width           = width;
                outTexture.height          = height;
                outTexture.depth           = 1;
                outTexture.mipLevels       = 1;
                outTexture.arrayLayers     = 1;
                outTexture.isCubemap       = false;
                outTexture.generateMipmaps = m_Options.generateMipmaps;
                outTexture.type            = VTextureDimension::e2D;
                outTexture.format          = hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8;
                outTexture.fileFormat      = m_Options.targetTextureFileFormat;
                outTexture.data.assign(reinterpret_cast<uint8_t*>(pixels),
                                       reinterpret_cast<uint8_t*>(pixels) +
                                           (hdr ? width * height * 4 * sizeof(float) : width * height * 4));
                stbi_image_free(pixels);
                return true;
            }

            // Convert to ktx2
            ktxTexture2*         kTexture = nullptr;
            ktxTextureCreateInfo ci {};
            ci.baseWidth       = width;
            ci.baseHeight      = height;
            ci.baseDepth       = 1;
            ci.numLevels       = 1;
            ci.numLayers       = 1;
            ci.numFaces        = 1;
            ci.numDimensions   = 2;
            ci.isArray         = KTX_FALSE;
            ci.generateMipmaps = m_Options.generateMipmaps;
            ci.vkFormat        = static_cast<uint32_t>(hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8);

            if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kTexture) != KTX_SUCCESS)
            {
                stbi_image_free(pixels);
                return false;
            }

            if (ktxTexture_SetImageFromMemory(ktxTexture(kTexture),
                                              0,
                                              0,
                                              0,
                                              reinterpret_cast<const ktx_uint8_t*>(pixels),
                                              hdr ? width * height * 4 * sizeof(float) : width * height * 4) !=
                KTX_SUCCESS)
            {
                stbi_image_free(pixels);
                ktxTexture_Destroy(ktxTexture(kTexture));
                return false;
            }
            stbi_image_free(pixels);

            // BasisU Compression
            ktxBasisParams params {};
            params.uastc = KTX_TRUE;
            params.noSSE = KTX_TRUE;
            ktxTexture2_CompressBasisEx(kTexture, &params);

            // Write to memory
            ktx_uint8_t* bytes = nullptr;
            ktx_size_t   size  = 0;
            if (ktxTexture_WriteToMemory(ktxTexture(kTexture), &bytes, &size) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(kTexture));
                return false;
            }

            // Fill outTexture
            outTexture.uuid            = VUUID::generate();
            outTexture.width           = width;
            outTexture.height          = height;
            outTexture.depth           = ci.baseDepth;
            outTexture.mipLevels       = ci.numLevels;
            outTexture.arrayLayers     = ci.numLayers;
            outTexture.isCubemap       = ci.numFaces == 6;
            outTexture.generateMipmaps = ci.generateMipmaps;
            outTexture.type            = static_cast<VTextureDimension>(ci.numDimensions);
            outTexture.format          = VTextureFormat::eRGBA8;
            outTexture.fileFormat      = VTextureFileFormat::eKTX2;
            outTexture.data.assign(bytes, bytes + size);

            ktxTexture_Destroy(ktxTexture(kTexture));
        }

        return true;
    }

    VMeshImporter& VMeshImporter::setOptions(const ImportOptions& /*options*/)
    {
        // Set import options if needed
        return *this;
    }

    bool VMeshImporter::importMesh(const std::string& /*filePath*/, VMesh& outMesh)
    {
        // TODO
        outMesh.uuid = VUUID::generate();
        return true;
    }
} // namespace vasset