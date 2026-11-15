#pragma once
#ifndef ES_CORE_RESOURCES_TEXTURE_DATA_H
#define ES_CORE_RESOURCES_TEXTURE_DATA_H

#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include "ImageIO.h"
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

	// Last committed sizes: accounting must not wait for an SVG decoder holding mMutex.
	size_t getMemoryUsage(MemoryUsageType type = MemoryUsageType::Allocated)
	{
		if (type == MemoryUsageType::RAM) return mDecodedBytes.load(std::memory_order_relaxed);
		if (type == MemoryUsageType::VRAM) return mTextureBytes.load(std::memory_order_relaxed);
		if (type == MemoryUsageType::Estimated) return mEstimatedBytes.load(std::memory_order_relaxed);
		return mAllocatedBytes.load(std::memory_order_relaxed);
	}

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

private:
	// The owner calls this with mMutex held. These counters describe published
	// buffers, not temporary decoder allocations or the process RSS.
	void publishMemoryUsage()
	{
		mDecodedBytes.store(calculateMemoryUsage(MemoryUsageType::RAM), std::memory_order_relaxed);
		mTextureBytes.store(calculateMemoryUsage(MemoryUsageType::VRAM), std::memory_order_relaxed);
		mEstimatedBytes.store(calculateMemoryUsage(MemoryUsageType::Estimated), std::memory_order_relaxed);
		mAllocatedBytes.store(calculateMemoryUsage(MemoryUsageType::Allocated), std::memory_order_relaxed);
	}
#if defined(USE_OPENGLES_30)
	size_t calculateMemoryUsage(MemoryUsageType type) const;
#else
	size_t calculateMemoryUsage(MemoryUsageType type) const
	{
		const size_t bytes = (mSize.x() > 0 ? (size_t)mSize.x() : 0) * (mSize.y() > 0 ? (size_t)mSize.y() : 0) * 4;
		if (type == MemoryUsageType::RAM) return mDataRGBA != nullptr ? bytes : 0;
		if (type == MemoryUsageType::VRAM) return mTextureID != 0 ? bytes : 0;
		if (type == MemoryUsageType::Estimated) return bytes;
		return mTextureID != 0 || mDataRGBA != nullptr ? bytes : 0;
	}
#endif
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
	std::atomic<size_t> mDecodedBytes{0}, mTextureBytes{0}, mEstimatedBytes{0}, mAllocatedBytes{0};
};

#endif // ES_CORE_RESOURCES_TEXTURE_DATA_H
