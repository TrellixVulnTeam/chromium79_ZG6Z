//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TextureVk.h:
//    Defines the class interface for TextureVk, implementing TextureImpl.
//

#ifndef LIBANGLE_RENDERER_VULKAN_TEXTUREVK_H_
#define LIBANGLE_RENDERER_VULKAN_TEXTUREVK_H_

#include "libANGLE/renderer/TextureImpl.h"
#include "libANGLE/renderer/vulkan/CommandGraph.h"
#include "libANGLE/renderer/vulkan/RenderTargetVk.h"
#include "libANGLE/renderer/vulkan/SamplerVk.h"
#include "libANGLE/renderer/vulkan/vk_helpers.h"

namespace rx
{

class TextureVk : public TextureImpl
{
  public:
    TextureVk(const gl::TextureState &state, RendererVk *renderer);
    ~TextureVk() override;
    void onDestroy(const gl::Context *context) override;

    angle::Result setImage(const gl::Context *context,
                           const gl::ImageIndex &index,
                           GLenum internalFormat,
                           const gl::Extents &size,
                           GLenum format,
                           GLenum type,
                           const gl::PixelUnpackState &unpack,
                           const uint8_t *pixels) override;
    angle::Result setSubImage(const gl::Context *context,
                              const gl::ImageIndex &index,
                              const gl::Box &area,
                              GLenum format,
                              GLenum type,
                              const gl::PixelUnpackState &unpack,
                              gl::Buffer *unpackBuffer,
                              const uint8_t *pixels) override;

    angle::Result setCompressedImage(const gl::Context *context,
                                     const gl::ImageIndex &index,
                                     GLenum internalFormat,
                                     const gl::Extents &size,
                                     const gl::PixelUnpackState &unpack,
                                     size_t imageSize,
                                     const uint8_t *pixels) override;
    angle::Result setCompressedSubImage(const gl::Context *context,
                                        const gl::ImageIndex &index,
                                        const gl::Box &area,
                                        GLenum format,
                                        const gl::PixelUnpackState &unpack,
                                        size_t imageSize,
                                        const uint8_t *pixels) override;

    angle::Result copyImage(const gl::Context *context,
                            const gl::ImageIndex &index,
                            const gl::Rectangle &sourceArea,
                            GLenum internalFormat,
                            gl::Framebuffer *source) override;
    angle::Result copySubImage(const gl::Context *context,
                               const gl::ImageIndex &index,
                               const gl::Offset &destOffset,
                               const gl::Rectangle &sourceArea,
                               gl::Framebuffer *source) override;

    angle::Result copyTexture(const gl::Context *context,
                              const gl::ImageIndex &index,
                              GLenum internalFormat,
                              GLenum type,
                              size_t sourceLevel,
                              bool unpackFlipY,
                              bool unpackPremultiplyAlpha,
                              bool unpackUnmultiplyAlpha,
                              const gl::Texture *source) override;
    angle::Result copySubTexture(const gl::Context *context,
                                 const gl::ImageIndex &index,
                                 const gl::Offset &destOffset,
                                 size_t sourceLevel,
                                 const gl::Box &sourceBox,
                                 bool unpackFlipY,
                                 bool unpackPremultiplyAlpha,
                                 bool unpackUnmultiplyAlpha,
                                 const gl::Texture *source) override;

    angle::Result copyCompressedTexture(const gl::Context *context,
                                        const gl::Texture *source) override;

    angle::Result setStorage(const gl::Context *context,
                             gl::TextureType type,
                             size_t levels,
                             GLenum internalFormat,
                             const gl::Extents &size) override;

    angle::Result setStorageExternalMemory(const gl::Context *context,
                                           gl::TextureType type,
                                           size_t levels,
                                           GLenum internalFormat,
                                           const gl::Extents &size,
                                           gl::MemoryObject *memoryObject,
                                           GLuint64 offset) override;

    angle::Result setEGLImageTarget(const gl::Context *context,
                                    gl::TextureType type,
                                    egl::Image *image) override;

    angle::Result setImageExternal(const gl::Context *context,
                                   gl::TextureType type,
                                   egl::Stream *stream,
                                   const egl::Stream::GLTextureDescription &desc) override;

    angle::Result generateMipmap(const gl::Context *context) override;

    angle::Result setBaseLevel(const gl::Context *context, GLuint baseLevel) override;

    angle::Result bindTexImage(const gl::Context *context, egl::Surface *surface) override;
    angle::Result releaseTexImage(const gl::Context *context) override;

    angle::Result getAttachmentRenderTarget(const gl::Context *context,
                                            GLenum binding,
                                            const gl::ImageIndex &imageIndex,
                                            GLsizei samples,
                                            FramebufferAttachmentRenderTarget **rtOut) override;

    angle::Result syncState(const gl::Context *context,
                            const gl::Texture::DirtyBits &dirtyBits) override;

    angle::Result setStorageMultisample(const gl::Context *context,
                                        gl::TextureType type,
                                        GLsizei samples,
                                        GLint internalformat,
                                        const gl::Extents &size,
                                        bool fixedSampleLocations) override;

    angle::Result initializeContents(const gl::Context *context,
                                     const gl::ImageIndex &imageIndex) override;

    const vk::ImageHelper &getImage() const
    {
        ASSERT(mImage && mImage->valid());
        return *mImage;
    }

    vk::ImageHelper &getImage()
    {
        ASSERT(mImage && mImage->valid());
        return *mImage;
    }

    void releaseOwnershipOfImage(const gl::Context *context);

    const vk::ImageView &getReadImageView() const;
    // A special view for cube maps as a 2D array, used with shaders that do texelFetch() and for
    // seamful cube map emulation.
    const vk::ImageView &getFetchImageView() const;
    angle::Result getStorageImageView(ContextVk *contextVk,
                                      bool allLayers,
                                      size_t level,
                                      size_t singleLayer,
                                      const vk::ImageView **imageViewOut);
    const vk::Sampler &getSampler() const;

    angle::Result ensureImageInitialized(ContextVk *contextVk);

    Serial getSerial() const { return mSerial; }

    void overrideStagingBufferSizeForTesting(size_t initialSizeForTesting)
    {
        mStagingBufferInitialSize = initialSizeForTesting;
    }

  private:
    // Transform an image index from the frontend into one that can be used on the backing
    // ImageHelper, taking into account mipmap or cube face offsets
    gl::ImageIndex getNativeImageIndex(const gl::ImageIndex &inputImageIndex) const;
    uint32_t getNativeImageLevel(uint32_t frontendLevel) const;
    uint32_t getNativeImageLayer(uint32_t frontendLayer) const;

    void releaseAndDeleteImage(ContextVk *contextVk);
    angle::Result ensureImageAllocated(ContextVk *contextVk, const vk::Format &format);
    void setImageHelper(ContextVk *contextVk,
                        vk::ImageHelper *imageHelper,
                        gl::TextureType imageType,
                        const vk::Format &format,
                        uint32_t imageLevelOffset,
                        uint32_t imageLayerOffset,
                        uint32_t imageBaseLevel,
                        bool selfOwned);
    void updateImageHelper(ContextVk *contextVk, const vk::Format &internalFormat);

    angle::Result redefineImage(const gl::Context *context,
                                const gl::ImageIndex &index,
                                const vk::Format &format,
                                const gl::Extents &size);

    angle::Result setImageImpl(const gl::Context *context,
                               const gl::ImageIndex &index,
                               const gl::InternalFormat &formatInfo,
                               const gl::Extents &size,
                               GLenum type,
                               const gl::PixelUnpackState &unpack,
                               const uint8_t *pixels);
    angle::Result setSubImageImpl(const gl::Context *context,
                                  const gl::ImageIndex &index,
                                  const gl::Box &area,
                                  const gl::InternalFormat &formatInfo,
                                  GLenum type,
                                  const gl::PixelUnpackState &unpack,
                                  const uint8_t *pixels,
                                  const vk::Format &vkFormat);

    angle::Result copyImageDataToBufferAndGetData(ContextVk *contextVk,
                                                  size_t sourceLevel,
                                                  uint32_t layerCount,
                                                  const gl::Rectangle &sourceArea,
                                                  uint8_t **outDataPtr);

    angle::Result copyImageDataToBuffer(ContextVk *contextVk,
                                        size_t sourceLevel,
                                        uint32_t layerCount,
                                        uint32_t baseLayer,
                                        const gl::Box &sourceArea,
                                        vk::BufferHelper **bufferOut,
                                        VkDeviceSize *bufferOffsetOut,
                                        uint8_t **outDataPtr);

    angle::Result generateMipmapsWithCPU(const gl::Context *context);

    angle::Result generateMipmapLevelsWithCPU(ContextVk *contextVk,
                                              const angle::Format &sourceFormat,
                                              GLuint layer,
                                              GLuint firstMipLevel,
                                              GLuint maxMipLevel,
                                              size_t sourceWidth,
                                              size_t sourceHeight,
                                              size_t sourceRowPitch,
                                              uint8_t *sourceData);

    angle::Result copySubImageImpl(const gl::Context *context,
                                   const gl::ImageIndex &index,
                                   const gl::Offset &destOffset,
                                   const gl::Rectangle &sourceArea,
                                   const gl::InternalFormat &internalFormat,
                                   gl::Framebuffer *source);

    angle::Result copySubTextureImpl(ContextVk *contextVk,
                                     const gl::ImageIndex &index,
                                     const gl::Offset &destOffset,
                                     const gl::InternalFormat &destFormat,
                                     size_t sourceLevel,
                                     const gl::Rectangle &sourceArea,
                                     bool unpackFlipY,
                                     bool unpackPremultiplyAlpha,
                                     bool unpackUnmultiplyAlpha,
                                     TextureVk *source);

    angle::Result copySubImageImplWithTransfer(ContextVk *contextVk,
                                               const gl::ImageIndex &index,
                                               const gl::Offset &destOffset,
                                               const vk::Format &destFormat,
                                               size_t sourceLevel,
                                               size_t sourceLayer,
                                               const gl::Rectangle &sourceArea,
                                               vk::ImageHelper *srcImage);

    angle::Result copySubImageImplWithDraw(ContextVk *contextVk,
                                           const gl::ImageIndex &index,
                                           const gl::Offset &destOffset,
                                           const vk::Format &destFormat,
                                           size_t sourceLevel,
                                           const gl::Rectangle &sourceArea,
                                           bool isSrcFlipY,
                                           bool unpackFlipY,
                                           bool unpackPremultiplyAlpha,
                                           bool unpackUnmultiplyAlpha,
                                           vk::ImageHelper *srcImage,
                                           const vk::ImageView *srcView);

    angle::Result initImage(ContextVk *contextVk,
                            const vk::Format &format,
                            const bool sized,
                            const gl::Extents &extents,
                            const uint32_t levelCount);
    void releaseImage(ContextVk *contextVk);
    void releaseStagingBuffer(ContextVk *contextVk);
    uint32_t getLevelCount() const;
    angle::Result initImageViews(ContextVk *contextVk,
                                 const vk::Format &format,
                                 const bool sized,
                                 uint32_t levelCount,
                                 uint32_t layerCount);
    angle::Result initRenderTargets(ContextVk *contextVk, GLuint layerCount, GLuint levelIndex);
    angle::Result getLevelLayerImageView(vk::Context *context,
                                         size_t level,
                                         size_t layer,
                                         const vk::ImageView **imageViewOut);

    angle::Result ensureImageInitializedImpl(ContextVk *contextVk,
                                             const gl::Extents &baseLevelExtents,
                                             uint32_t levelCount,
                                             const vk::Format &format);

    void onStagingBufferChange() { onStateChange(angle::SubjectMessage::SubjectChanged); }

    angle::Result changeLevels(ContextVk *contextVk, GLuint baseLevel, GLuint maxLevel);

    bool mOwnsImage;

    gl::TextureType mImageNativeType;

    // The layer offset to apply when converting from a frontend texture layer to a texture layer in
    // mImage. Used when this texture sources a cube map face or 3D texture layer from an EGL image.
    uint32_t mImageLayerOffset;

    // The level offset to apply when converting from a frontend texture level to texture level in
    // mImage.
    uint32_t mImageLevelOffset;

    // |mImage| wraps a VkImage and VkDeviceMemory that represents the gl::Texture. |mOwnsImage|
    // indicates that |TextureVk| owns the image. Otherwise it is a weak pointer shared with another
    // class.
    vk::ImageHelper *mImage;

    // |mImageViews| contains all the current views for the Texture. The views are always owned by
    // the Texture and are not shared like |mImage|. They also have different lifetimes and can be
    // reallocated independently of |mImage| on state changes.
    vk::ImageViewHelper mImageViews;

    // |mSampler| contains the relevant Vulkan sampler states reprensenting the OpenGL Texture
    // sampling states for the Texture.
    vk::Sampler mSampler;

    // Render targets stored as vector of vectors
    // Level is first dimension, layer is second
    std::vector<RenderTargetVector> mRenderTargets;

    // The serial is used for cache indexing.
    Serial mSerial;

    // Overridden in some tests.
    size_t mStagingBufferInitialSize;
};

}  // namespace rx

#endif  // LIBANGLE_RENDERER_VULKAN_TEXTUREVK_H_
