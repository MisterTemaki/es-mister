#pragma once
#ifndef ES_CORE_RESOURCES_PICTURE_CACHE_H
#define ES_CORE_RESOURCES_PICTURE_CACHE_H

#include "math/Vector2i.h"
#include <cstddef>
#include <cstdint>
#include <string>

// Pictures as ImageIO::loadFromMemoryRGBA32 gives them (decoded, shrunk to the size asked, rows bottom up),
// kept on the disk without loss (QOI) so that the next time the same file at the same size costs a read
// and a QOI decode instead of a PNG/JPEG decode and a resize: ~7 ms instead of ~130 ms for a cover on the
// MiSTer. The bytes are the very bytes ImageIO gave (checked by decoding back before writing, and by a
// checksum when reading), so the pictures drawn are the same.
//
// On only when ES_PICTURE_CACHE names a folder (es-mister.sh: the install's cache/pictures). A cached
// picture is known by its file (path, size, modification time), the size it was asked at, the screen's
// size and the frame of an animation, so a changed file or another size is a new entry. Written by a
// thread of its own, at the lowest priority, and only while the disk keeps 1 GB free.
namespace PictureCache
{
	struct Key
	{
		std::string path;   // the file read (a resource's real path)
		uint64_t size = 0;
		int64_t mtimeNs = 0;
		float maxX = 0, maxY = 0;
		bool externalZoom = false;
		int screenWidth = 0, screenHeight = 0;
		int subImageIndex = -1;

		std::string text() const;
	};

	struct Picture
	{
		unsigned char* rgba = nullptr; // new[], as ImageIO gives it
		size_t width = 0, height = 0;
		Vector2i baseSize, packedSize;  // ImageIO's baseSize and packedSize
	};

	bool enabled();

	// The key of FILE (stat), false when the cache is off or the file cannot be read.
	bool makeKey(const std::string& file, Key& key);

	// The picture when it is in the cache and whole.
	bool load(const Key& key, Picture& picture);

	// A copy of the picture queued to be written (dropped when too many wait).
	void store(const Key& key, const Picture& picture);
}

#endif
