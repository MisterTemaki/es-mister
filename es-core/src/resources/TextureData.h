#pragma once
#ifndef ES_CORE_RESOURCES_TEXTURE_DATA_H
#define ES_CORE_RESOURCES_TEXTURE_DATA_H

#include <mutex>
#include <string>
#include <vector>
#include "ImageIO.h"
#include "resources/PictureCache.h"
#include "TextureDataManager.h"

class TextureResource;

class IPdfHandler
{
public:
	virtual int getPdfPageCount(const std::string& fileName) = 0;
	virtual std::vector<std::string> extractPdfImages(const std::string& fileName, int pageIndex = -1, int pageCount = 1, int quality = 0) = 0;
};

class TextureData
{
public:
	static IPdfHandler* PdfHandler;

	TextureData(bool tile, bool linear);
	~TextureData();

	// These functions populate mDataRGBA but do not upload the texture to VRAM

	//!!!! Needs to be canonical path. Caller should check for duplicates before calling this
	void initFromPath(const std::string& path);
	bool initSVGFromMemory(const unsigned char* fileData, size_t length);
	bool initImageFromMemory(const unsigned char* fileData, size_t length, int subImageIndex = -1);
	bool initFromRGBA(unsigned char* dataRGBA, size_t width, size_t height, bool copyData = true);

	// Read the data into memory if necessary
	bool load();
	bool loadFromCbz();
	bool loadFromPdf(int pageIndex = 1);
	bool loadFromVideo();

	bool isLoaded();

	// Upload the texture to VRAM if necessary and bind. Returns true if bound ok or
	// false if either not loaded
	bool uploadAndBind();

	// Release the texture from VRAM
	void releaseVRAM();

	// Release the texture from conventional RAM
	void releaseRAM();

	// Get the amount of memory used by this texture.
#if defined(USE_OPENGLES_30)
	size_t getMemoryUsage(MemoryUsageType type = MemoryUsageType::Allocated);
#else
	size_t getMemoryUsage(MemoryUsageType type = MemoryUsageType::Allocated)
	{
		if (type == MemoryUsageType::RAM)
			return mDataRGBA != nullptr ? mSize.x() * mSize.y() * 4 : 0;

		if (type == MemoryUsageType::VRAM)
			return mTextureID != 0 ? mSize.x() * mSize.y() * 4 : 0;

		if (type == MemoryUsageType::Estimated)
			return mSize.x() * mSize.y() * 4;

		return mTextureID != 0 || mDataRGBA != nullptr ? mSize.x() * mSize.y() * 4 : 0;
	}
#endif

	const 	Vector2i& getSize() const { return mSize; }
	const 	Vector2f& getPhysicalSize() const { return mPhysicalSize; }

	bool rasterizeAt(float width, float height);

	bool tiled() { return mTile; }

	unsigned char* getDataRGBA() { return mDataRGBA; }

	void setMaxSize(const MaxSizeInfo& maxSize);
	bool isMaxSizeValid();

	inline const std::string& getPath() { return mPath; };

	bool updateFromExternalRGBA(unsigned char* dataRGBA, size_t width, size_t height);

	inline bool isRequired() { return mRequired; };
	void setRequired(bool value) { mRequired = value; };

	inline bool isDynamic() { return mDynamic; };
	void setDynamic(bool value) { mDynamic = value; };

	inline bool isReloadable() { return mReloadable; };
	
	inline bool isScalable() { return mScalable; }
	void setScalable(bool value) { mScalable = value; };

	// The Allocated and RAM memory of the textures in TextureDataManager's list, summed as they change
	// (cleanupVRAM asks for them every frame; summing them there walked every texture ever created).
	// recount() after each change of mTextureID, mDataRGBA or mSize; setCounted() as one enters or leaves
	// the list. The sums equal getMemoryUsage over the list (checked every frame in MISTER_PERF builds).
	void recount();
	void setCounted(bool counted);
	static size_t countedTotal(MemoryUsageType type);

private:
	// the size a raster picture is decoded at (at most the screen's, or the size asked with OptimizeVRAM)
	MaxSizeInfo pictureMaxSize();
	// initImageFromMemory at that size; with a cache key, the decoded picture also goes to the PictureCache
	bool initImageFromMemory(const unsigned char* fileData, size_t length, int subImageIndex, const MaxSizeInfo& maxSize, const PictureCache::Key* cacheKey);
	// load()'s raster picture from the PictureCache, when it is there
	bool loadFromPictureCache(const std::string& file, PictureCache::Key& key, int subImageIndex);

	bool			mRequired;

	std::mutex		mMutex;
	bool			mTile;
	bool			mLinear;
	std::string		mPath;
	unsigned int	mTextureID;
	unsigned char*	mDataRGBA;
	bool			mReloadable;
	bool			mDynamic;
#if defined(USE_OPENGLES_30)
	bool			mMipmapped;
#endif

	MaxSizeInfo		mMaxSize;

	Vector2i		mSize;
	Vector2f		mPhysicalSize;

	bool			mScalable;
	Vector2f		mScalableMinimumSize;

	bool			mIsExternalDataRGBA;

	// what this texture adds to the sums while it is in the list (under the sums' mutex)
	bool			mCounted = false;
	size_t			mCountedAllocated = 0;
	size_t			mCountedRAM = 0;
};

#endif // ES_CORE_RESOURCES_TEXTURE_DATA_H
