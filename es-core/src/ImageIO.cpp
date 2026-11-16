#include "ImageIO.h"

#include "Log.h"
#ifdef ES_USE_STB_IMAGE
#include <stb_image.h>
#include <stb_image_resize2.h>
#else
#include <FreeImage.h>
#endif
#include <string.h>
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <sstream>
#include <fstream>
#include <map>
#include <unordered_map>
#include <mutex>
#include "renderers/Renderer.h"
#include "Paths.h"
#include "math/Vector4f.h"

const MaxSizeInfo MaxSizeInfo::Empty;

#ifdef ES_USE_STB_IMAGE

// stb_image in place of FreeImage (the MiSTer build: nothing to install, and small). The result is the
// same as FreeImage's: RGBA bytes, the rows bottom up, shrunk to the size asked.
unsigned char* ImageIO::loadFromMemoryRGBA32(const unsigned char * data, const size_t size, size_t & width, size_t & height, MaxSizeInfo* maxSize, Vector2i* baseSize, Vector2i* packedSize, int subImageIndex)
{
	LOG(LogDebug) << "ImageIO::loadFromMemoryRGBA32";

	if (baseSize != nullptr)
		*baseSize = Vector2i(0, 0);

	if (packedSize != nullptr)
		*packedSize = Vector2i(0, 0);

	width = 0;
	height = 0;

	if (data == nullptr || size == 0)
		return nullptr;

	int w = 0, h = 0, components = 0;
	unsigned char* rgba = nullptr;
	bool ownBuffer = false; // rgba from new[] rather than stbi

	// an animated GIF's frame
	if (subImageIndex >= 0 && size > 6 && memcmp(data, "GIF", 3) == 0)
	{
		int* delays = nullptr;
		int frames = 0;
		unsigned char* all = stbi_load_gif_from_memory(data, (int)size, &delays, &w, &h, &frames, &components, 4);
		if (all != nullptr && frames > 0)
		{
			const int index = subImageIndex < frames ? subImageIndex : frames - 1;
			const size_t frameBytes = (size_t)w * h * 4;
			rgba = new unsigned char[frameBytes];
			memcpy(rgba, all + frameBytes * index, frameBytes);
			ownBuffer = true;
		}
		if (all != nullptr)
			stbi_image_free(all);
		if (delays != nullptr)
			stbi_image_free(delays);
	}

	if (rgba == nullptr)
		rgba = stbi_load_from_memory(data, (int)size, &w, &h, &components, 4);

	if (rgba == nullptr)
	{
		LOG(LogError) << "Error - Failed to load image from memory: " << (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
		return nullptr;
	}

	width = w;
	height = h;

	if (baseSize != nullptr)
		*baseSize = Vector2i(w, h);

	size_t maxX = maxSize == nullptr ? 0 : (size_t)Math::round(maxSize->x());
	size_t maxY = maxSize == nullptr ? 0 : (size_t)Math::round(maxSize->y());

	if (maxSize != nullptr && maxX > 0 && maxY > 0 && (width > maxX || height > maxY))
	{
		Vector2i sz = adjustPictureSize(Vector2i(width, height), Vector2i(maxX, maxY), maxSize->externalZoom());

		if (sz.x() > Renderer::getScreenWidth() || sz.y() > Renderer::getScreenHeight())
			sz = adjustPictureSize(sz, Vector2i(Renderer::getScreenWidth(), Renderer::getScreenHeight()), false);

		if (sz.x() > 0 && sz.y() > 0 && (sz.x() != (int)width || sz.y() != (int)height))
		{
			LOG(LogDebug) << "ImageIO : rescaling image from " << width << "x" << height << " to " << sz.x() << "x" << sz.y();

			unsigned char* scaled = new unsigned char[(size_t)sz.x() * sz.y() * 4];
			// written bottom up (its last row first, a negative stride), as handed over below: the same
			// bytes as turning it over after, without that copy
			const int scaledStride = sz.x() * 4;
			if (stbir_resize(rgba, w, h, 0, scaled + (size_t)(sz.y() - 1) * scaledStride, sz.x(), sz.y(), -scaledStride, STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX) != nullptr)
			{
				if (ownBuffer)
					delete[] rgba;
				else
					stbi_image_free(rgba);
				rgba = scaled;
				ownBuffer = true;
				width = sz.x();
				height = sz.y();

				if (packedSize != nullptr)
					*packedSize = Vector2i(width, height);

				// already bottom up, and from new[] as the caller frees it
				return scaled;
			}
			else
				delete[] scaled;
		}
	}

	// bottom up, as FreeImage gives them
	const size_t rowBytes = width * 4;
	unsigned char* result = new unsigned char[rowBytes * height];
	for (size_t y = 0; y < height; y++)
		memcpy(result + y * rowBytes, rgba + (height - 1 - y) * rowBytes, rowBytes);

	if (ownBuffer)
		delete[] rgba;
	else
		stbi_image_free(rgba);

	return result;
}

#else

unsigned char* ImageIO::loadFromMemoryRGBA32(const unsigned char * data, const size_t size, size_t & width, size_t & height, MaxSizeInfo* maxSize, Vector2i* baseSize, Vector2i* packedSize, int subImageIndex)
{
	LOG(LogDebug) << "ImageIO::loadFromMemoryRGBA32";

	if (baseSize != nullptr)
		*baseSize = Vector2i(0, 0);

	if (baseSize != nullptr)
		*packedSize = Vector2i(0, 0);

	std::vector<unsigned char> rawData;
	width = 0;
	height = 0;
	FIMEMORY * fiMemory = FreeImage_OpenMemory((BYTE *)data, (DWORD)size);
	if (fiMemory != nullptr) 
	{
		//detect the filetype from data
		FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromMemory(fiMemory);
		if (format != FIF_UNKNOWN && FreeImage_FIFSupportsReading(format))
		{			
			// file type is supported. load image
			FIMULTIBITMAP* fiMultiBitmap = nullptr;
			FIBITMAP* fiBitmap = nullptr;

			if (subImageIndex < 0)
				fiBitmap = FreeImage_LoadFromMemory(format, fiMemory);
			else 
			{
				fiMultiBitmap = FreeImage_LoadMultiBitmapFromMemory(format, fiMemory, GIF_PLAYBACK);
				if (fiMultiBitmap == nullptr)
					fiBitmap = FreeImage_LoadFromMemory(format, fiMemory);
				else
				{
					fiBitmap = FreeImage_LockPage(fiMultiBitmap, subImageIndex);
					if (fiBitmap == nullptr)
						FreeImage_CloseMultiBitmap(fiMultiBitmap);				
				}
			}
			
			if (fiBitmap != nullptr)
			{
				//loaded. convert to 32bit if necessary
				const int bitsPerPixel = FreeImage_GetBPP(fiBitmap);
				if ((fiMultiBitmap != nullptr && bitsPerPixel != 32) || (fiMultiBitmap == nullptr && bitsPerPixel != 32 && bitsPerPixel != 24 && bitsPerPixel != 8))
				{
					FIBITMAP * fiConverted = FreeImage_ConvertTo32Bits(fiBitmap);
					if (fiConverted != nullptr)
					{
						//free original bitmap data
						if (fiMultiBitmap != nullptr)
						{
							FreeImage_UnlockPage(fiMultiBitmap, fiBitmap, false);
							FreeImage_CloseMultiBitmap(fiMultiBitmap);
							fiMultiBitmap = nullptr;
						}
						else
							FreeImage_Unload(fiBitmap);

						fiBitmap = fiConverted;
					}
				}

				if (fiBitmap != nullptr)
				{
					width = FreeImage_GetWidth(fiBitmap);
					height = FreeImage_GetHeight(fiBitmap);

					if (baseSize != nullptr)
						*baseSize = Vector2i(width, height);

					size_t maxX = maxSize == nullptr ? 0 : (size_t) Math::round(maxSize->x());
					size_t maxY = maxSize == nullptr ? 0 : (size_t) Math::round(maxSize->y());

					if (maxSize != nullptr && maxX > 0 && maxY > 0 && (width > maxX || height > maxY))
					{
						Vector2i sz = adjustPictureSize(Vector2i(width, height), Vector2i(maxX, maxY), maxSize->externalZoom());

						if (sz.x() > Renderer::getScreenWidth() || sz.y() > Renderer::getScreenHeight())
							sz = adjustPictureSize(sz, Vector2i(Renderer::getScreenWidth(), Renderer::getScreenHeight()), false);
						
						if (sz.x() != width || sz.y() != height)
						{
							LOG(LogDebug) << "ImageIO : rescaling image from " << std::string(std::to_string(width) + "x" + std::to_string(height)).c_str() << " to " << std::string(std::to_string(sz.x()) + "x" + std::to_string(sz.y())).c_str();

							FIBITMAP* imageRescaled = FreeImage_Rescale(fiBitmap, sz.x(), sz.y(), FILTER_BOX);

							if (fiMultiBitmap != nullptr)
							{
								FreeImage_UnlockPage(fiMultiBitmap, fiBitmap, false);
								FreeImage_CloseMultiBitmap(fiMultiBitmap);
								fiMultiBitmap = nullptr;
							}
							else
								FreeImage_Unload(fiBitmap);

							fiBitmap = imageRescaled;

							width = FreeImage_GetWidth(fiBitmap);
							height = FreeImage_GetHeight(fiBitmap);

							if (packedSize != nullptr)
								*packedSize = Vector2i(width, height);
						}
					}

					unsigned char* tempData = new unsigned char[width * height * 4];
											
					const size_t w = width;
					const size_t h = height;
					const int pitch = FreeImage_GetPitch(fiBitmap);
					const unsigned char* bits = FreeImage_GetBits(fiBitmap);
					const int bpp = FreeImage_GetBPP(fiBitmap);

					if (bpp == 32)
					{
						for (size_t y = 0; y < h; y++)
						{
							const unsigned int* argb = (const unsigned int*)(bits + y * pitch);
							unsigned int* abgr = (unsigned int*)(tempData + y * w * 4);

							for (size_t x = 0; x < w; x++)
							{
								const unsigned int c = argb[x];
								abgr[x] = (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
							}
						}
					}
					else if (bpp == 24)
					{
						for (size_t y = 0; y < h; y++)
						{
							const unsigned char* src = bits + y * pitch;
							unsigned int* abgr = (unsigned int*)(tempData + y * w * 4);
							for (size_t x = 0; x < w; x++)
							{
								const unsigned char r = src[x * 3 + 0];
								const unsigned char g = src[x * 3 + 1];
								const unsigned char b = src[x * 3 + 2];
								abgr[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
							}
						}
					}
					else if (bpp == 8)
					{
						RGBQUAD* palette = FreeImage_GetPalette(fiBitmap);
						BOOL hasTransparency = FreeImage_IsTransparent(fiBitmap);
						BYTE* transTable = FreeImage_GetTransparencyTable(fiBitmap); // alpha par index de palette

						for (size_t y = 0; y < h; y++)
						{
							const unsigned char* src = bits + y * pitch;
							unsigned int* abgr = (unsigned int*)(tempData + y * w * 4);
							for (size_t x = 0; x < w; x++)
							{
								const BYTE idx = src[x];
								const RGBQUAD& color = palette[idx];
								const BYTE alpha = (hasTransparency && transTable) ? transTable[idx] : 0xFF;
								abgr[x] = (alpha << 24) | (color.rgbBlue << 16) | (color.rgbGreen << 8) | color.rgbRed;
							}
						}
					}

					if (fiMultiBitmap)
					{
						FreeImage_UnlockPage(fiMultiBitmap, fiBitmap, false);
						FreeImage_CloseMultiBitmap(fiMultiBitmap);
					}
					else
						FreeImage_Unload(fiBitmap);

					FreeImage_CloseMemory(fiMemory);

					return tempData;
				}
			}
			else
			{
				LOG(LogError) << "Error - Failed to load image from memory!";
			}
		}
		else
		{
			LOG(LogError) << "Error - File type " << (format == FIF_UNKNOWN ? "unknown" : "unsupported") << "!";
		}
		//free FIMEMORY again
		FreeImage_CloseMemory(fiMemory);
	}

	return nullptr;
}

#endif // ES_USE_STB_IMAGE

void ImageIO::flipPixelsVert(unsigned char* imagePx, const size_t& width, const size_t& height)
{
	unsigned int temp;
	unsigned int* arr = (unsigned int*)imagePx;
	for(size_t y = 0; y < height / 2; y++)
	{
		for(size_t x = 0; x < width; x++)
		{
			temp = arr[x + (y * width)];
			arr[x + (y * width)] = arr[x + (height * width) - ((y + 1) * width)];
			arr[x + (height * width) - ((y + 1) * width)] = temp;
		}
	}
}

Vector2f ImageIO::adjustPictureSizeF(Vector2f imageSize, Vector2f maxSize, bool externSize)
{
	return adjustPictureSizeF(imageSize.x(), imageSize.y(), maxSize.x(), maxSize.y(), externSize);
}

Vector2f ImageIO::adjustPictureSizeF(float cxDIB, float cyDIB, float iMaxX, float iMaxY, bool externSize)
{
	if (externSize)
		return getPictureMinSize(Vector2f(cxDIB, cyDIB), Vector2f(iMaxX, iMaxY));

	if (cxDIB == 0 || cyDIB == 0)
		return Vector2f(cxDIB, cyDIB);

	float xCoef = iMaxX / cxDIB;
	float yCoef = iMaxY / cyDIB;

	cyDIB = cyDIB * std::max(xCoef, yCoef);
	cxDIB = cxDIB * std::max(xCoef, yCoef);

	if (cxDIB > iMaxX)
	{
		cyDIB = cyDIB * iMaxX / cxDIB;
		cxDIB = iMaxX;
	}

	if (cyDIB > iMaxY)
	{
		cxDIB = cxDIB * iMaxY / cyDIB;
		cyDIB = iMaxY;
	}

	return Vector2f(cxDIB, cyDIB);
}

Vector2i ImageIO::adjustPictureSize(Vector2i imageSize, Vector2i maxSize, bool externSize)
{
	if (externSize)
	{
		Vector2f szf = getPictureMinSize(Vector2f(imageSize.x(), imageSize.y()), Vector2f(maxSize.x(), maxSize.y()));
		return Vector2i(szf.x(), szf.y());
	}

	int cxDIB = imageSize.x();
	int cyDIB = imageSize.y();

	if (cxDIB == 0 || cyDIB == 0)
		return imageSize;

	int iMaxX = maxSize.x();
	int iMaxY = maxSize.y();

	double xCoef = (double)iMaxX / (double)cxDIB;
	double yCoef = (double)iMaxY / (double)cyDIB;

#if WIN32
	cyDIB = (int)((double)cyDIB * std::fmax(xCoef, yCoef));
	cxDIB = (int)((double)cxDIB * std::fmax(xCoef, yCoef));
#else
	cyDIB = (int)((double)cyDIB * std::max(xCoef, yCoef));
	cxDIB = (int)((double)cxDIB * std::max(xCoef, yCoef));
#endif

	if (cxDIB > iMaxX)
	{
		cyDIB = (int)((double)cyDIB * (double)iMaxX / (double)cxDIB);
		cxDIB = iMaxX;
	}

	if (cyDIB > iMaxY)
	{
		cxDIB = (int)((double)cxDIB * (double)iMaxY / (double)cyDIB);
		cyDIB = iMaxY;
	}

	return Vector2i(cxDIB, cyDIB);
}

Vector2f ImageIO::getPictureMinSize(Vector2f imageSize, Vector2f maxSize)
{
	if (imageSize.x() == 0 || imageSize.y() == 0)
		return imageSize;

	float cxDIB = maxSize.x();
	float cyDIB = maxSize.y();

	float xCoef = maxSize.x() / imageSize.x();
	float yCoef = maxSize.y() / imageSize.y();

	if (imageSize.x() * yCoef < maxSize.x())
		cyDIB = imageSize.y() * xCoef;
	else
		cxDIB = imageSize.x() * yCoef;

	return Vector2f(cxDIB, cyDIB);
}

#ifdef ES_USE_STB_IMAGE

bool ImageIO::getMultiBitmapInformation(const std::string& path, int& totalFrames, int& frameTime)
{
	totalFrames = 1;
	frameTime = 0;

	// only GIFs are animated (as with FreeImage)
	auto size = Utils::FileSystem::getFileSize(path);
	if (size < 6)
		return false;

	std::ifstream stream(path, std::ios::binary);
	std::vector<unsigned char> data((size_t)size);
	stream.read((char*)data.data(), size);
	if (!stream || memcmp(data.data(), "GIF", 3) != 0)
		return false;

	int* delays = nullptr;
	int w = 0, h = 0, frames = 0, components = 0;
	unsigned char* all = stbi_load_gif_from_memory(data.data(), (int)data.size(), &delays, &w, &h, &frames, &components, 4);
	bool result = false;
	if (all != nullptr && frames > 1)
	{
		totalFrames = frames;
		frameTime = delays != nullptr ? delays[0] : 0;
		if (frameTime == 0)
			frameTime = 100;
		result = true;
	}
	if (all != nullptr)
		stbi_image_free(all);
	if (delays != nullptr)
		stbi_image_free(delays);
	return result;
}

#else

bool ImageIO::getMultiBitmapInformation(const std::string& path, int& totalFrames, int& frameTime)
{	
	totalFrames = 1;
	frameTime = 0;

	FREE_IMAGE_FORMAT fileFormat;

#if WIN32
	fileFormat = FreeImage_GetFileTypeU(Utils::String::convertToWideString(path).c_str());
#else
	fileFormat = FreeImage_GetFileType(path.c_str());
#endif

	if (fileFormat != FIF_GIF && fileFormat != FIF_PNG)
		return false;

	if (!FreeImage_FIFSupportsReading(fileFormat))
		return false;

	auto size = Utils::FileSystem::getFileSize(path);
	if (size < 4)
		return false;

	unsigned char* data = new unsigned char[size]();
	if (data == nullptr)
		return false;

#if WIN32
	std::ifstream stream(Utils::String::convertToWideString(path), std::ios::binary);
#else
	std::ifstream stream(path, std::ios::binary);
#endif

	stream.read((char*)data, size);
	stream.close();

	bool result = false;

	FIMEMORY* fiMemory = FreeImage_OpenMemory((BYTE *)data, (DWORD)size);
	if (fiMemory != nullptr)
	{
		auto fiMultiBitmap = FreeImage_LoadMultiBitmapFromMemory(fileFormat, fiMemory);
		if (fiMultiBitmap != nullptr)
		{
			auto fiBitmap = FreeImage_LockPage(fiMultiBitmap, 0);
			if (fiBitmap != nullptr)
			{
				totalFrames = FreeImage_GetPageCount(fiMultiBitmap);
				if (totalFrames > 1)
				{
					FITAG* tagFrameTime = nullptr;
					FreeImage_GetMetadata(FIMD_ANIMATION, fiBitmap, "FrameTime", &tagFrameTime);
					if (tagFrameTime != nullptr && FreeImage_GetTagCount(tagFrameTime))
						frameTime = *static_cast<const uint32_t*>(FreeImage_GetTagValue(tagFrameTime));

					if (frameTime == 0)
						frameTime = 100;

					result = true;
				}

				FreeImage_UnlockPage(fiMultiBitmap, fiBitmap, false);
			}

			FreeImage_CloseMultiBitmap(fiMultiBitmap, 0);
		}

		FreeImage_CloseMemory(fiMemory);
	}

	delete[] data;

	return result;
}

#endif // ES_USE_STB_IMAGE
