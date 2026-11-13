// stb's image reading, resizing and writing, for the builds that use it in place of FreeImage
// (ES_USE_STB_IMAGE: the MiSTer's), and the software renderer's screenshots.
#if defined(ES_USE_STB_IMAGE) || defined(USE_SOFTWARE_RENDERER)

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#endif
