#include "resources/PictureCache.h"

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
// what qoi_decode gives is handed to TextureData, which frees it with delete[]
#define QOI_MALLOC(sz) ((void*)new unsigned char[(sz)])
#define QOI_FREE(p) delete[] ((unsigned char*)(p))
#include <qoi/qoi.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

namespace PictureCache
{
	// the file's layout; a change of it, or of what ImageIO gives, takes a new version (old entries are then
	// never read again)
	static const char MAGIC[4] = { 'E', 'S', 'P', 'C' };
	static const uint32_t VERSION = 1;
	static const size_t QUEUED_MAX = 48;
	static const uint64_t FREE_MIN = 1024ull * 1024 * 1024;

	static std::string root()
	{
		static const std::string value = []
		{
			const char* env = getenv("ES_PICTURE_CACHE");
			std::string dir = env != nullptr ? env : "";
			while (dir.size() > 1 && dir.back() == '/')
				dir.pop_back();
			return dir;
		}();
		return value;
	}

	bool enabled() { return !root().empty(); }

	std::string Key::text() const
	{
		char numbers[192];
		uint32_t x, y;
		memcpy(&x, &maxX, 4);
		memcpy(&y, &maxY, 4);
		snprintf(numbers, sizeof(numbers), "|%llu|%lld|%08x|%08x|%d|%d|%d|%d|v%u",
			(unsigned long long)size, (long long)mtimeNs, x, y, externalZoom ? 1 : 0, screenWidth, screenHeight, subImageIndex, VERSION);
		return path + numbers;
	}

	bool makeKey(const std::string& file, Key& key)
	{
		if (!enabled() || file.empty())
			return false;
		struct stat st;
		if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
			return false;
		key.path = file;
		key.size = (uint64_t)st.st_size;
		key.mtimeNs = (int64_t)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
		return true;
	}

	static uint64_t hash64(const std::string& s)
	{
		uint64_t h = 1469598103934665603ull; // FNV-1a
		for (unsigned char c : s)
		{
			h ^= c;
			h *= 1099511628211ull;
		}
		return h;
	}

	static uint32_t checksum(const unsigned char* p, size_t n)
	{
		// FNV-1a over 32-bit words (and the tail), enough to tell a damaged file
		uint32_t h = 2166136261u;
		size_t i = 0;
		for (; i + 4 <= n; i += 4)
		{
			uint32_t w;
			memcpy(&w, p + i, 4);
			h = (h ^ w) * 16777619u;
		}
		for (; i < n; i++)
			h = (h ^ p[i]) * 16777619u;
		return h;
	}

	static std::string fileOf(const std::string& text, std::string* dir = nullptr)
	{
		char name[32];
		snprintf(name, sizeof(name), "%016llx", (unsigned long long)hash64(text));
		const std::string sub = root() + "/" + std::string(name, 2);
		if (dir != nullptr)
			*dir = sub;
		return sub + "/" + name + ".espc";
	}

	// the header: MAGIC, VERSION, the key's length and text, width, height, base and packed sizes, the
	// pixels' checksum, the QOI data's length; then the QOI data
	struct Header
	{
		uint32_t width, height;
		int32_t baseW, baseH, packedW, packedH;
		uint32_t pixelsChecksum, qoiLength;
	};

	static bool readAll(int fd, std::vector<unsigned char>& out)
	{
		struct stat st;
		if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 256 * 1024 * 1024)
			return false;
		out.resize((size_t)st.st_size);
		size_t done = 0;
		while (done < out.size())
		{
			const ssize_t n = read(fd, out.data() + done, out.size() - done);
			if (n <= 0)
				return false;
			done += (size_t)n;
		}
		return true;
	}

	bool load(const Key& key, Picture& picture)
	{
		if (!enabled())
			return false;

		const std::string text = key.text();
		const int fd = open(fileOf(text).c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return false;
		std::vector<unsigned char> data;
		const bool read = readAll(fd, data);
		close(fd);
		if (!read)
			return false;

		size_t at = 0;
		auto take = [&](void* to, size_t n) { if (at + n > data.size()) return false; memcpy(to, data.data() + at, n); at += n; return true; };

		char magic[4];
		uint32_t version = 0, textLength = 0;
		if (!take(magic, 4) || memcmp(magic, MAGIC, 4) != 0 || !take(&version, 4) || version != VERSION || !take(&textLength, 4) || textLength != text.size())
			return false;
		if (at + textLength > data.size() || memcmp(data.data() + at, text.data(), textLength) != 0)
			return false; // another file with the same hash
		at += textLength;

		Header h;
		if (!take(&h, sizeof(h)) || h.width == 0 || h.height == 0 || at + h.qoiLength != data.size())
			return false;

		qoi_desc desc;
		unsigned char* pixels = (unsigned char*)qoi_decode(data.data() + at, (int)h.qoiLength, &desc, 4);
		if (pixels == nullptr)
			return false;
		const size_t bytes = (size_t)h.width * h.height * 4;
		if (desc.width != h.width || desc.height != h.height || checksum(pixels, bytes) != h.pixelsChecksum)
		{
			delete[] pixels;
			return false;
		}

		picture.rgba = pixels;
		picture.width = h.width;
		picture.height = h.height;
		picture.baseSize = Vector2i(h.baseW, h.baseH);
		picture.packedSize = Vector2i(h.packedW, h.packedH);
		return true;
	}

	// ---- the writer: one thread, lowest priority, a short queue

	struct Job
	{
		std::string text;
		std::vector<unsigned char> rgba;
		Header header;
	};

	static void write(Job& job)
	{
		std::string dir;
		const std::string file = fileOf(job.text, &dir);

		mkdir(root().c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		struct statvfs vfs;
		if (statvfs(root().c_str(), &vfs) != 0 || (uint64_t)vfs.f_bavail * vfs.f_frsize < FREE_MIN)
			return;

		qoi_desc desc;
		desc.width = job.header.width;
		desc.height = job.header.height;
		desc.channels = 4;
		desc.colorspace = QOI_SRGB;
		int length = 0;
		unsigned char* qoi = (unsigned char*)qoi_encode(job.rgba.data(), &desc, &length);
		if (qoi == nullptr || length <= 0)
			return;

		// decoded back: written only when it gives the very bytes
		qoi_desc back;
		unsigned char* check = (unsigned char*)qoi_decode(qoi, length, &back, 4);
		const bool same = check != nullptr && back.width == desc.width && back.height == desc.height && memcmp(check, job.rgba.data(), job.rgba.size()) == 0;
		delete[] check;
		if (!same)
		{
			delete[] qoi;
			return;
		}

		job.header.qoiLength = (uint32_t)length;
		const uint32_t textLength = (uint32_t)job.text.size();
		std::vector<unsigned char> out;
		out.reserve(12 + textLength + sizeof(Header) + length);
		auto put = [&](const void* p, size_t n) { const unsigned char* b = (const unsigned char*)p; out.insert(out.end(), b, b + n); };
		put(MAGIC, 4);
		put(&VERSION, 4);
		put(&textLength, 4);
		put(job.text.data(), textLength);
		put(&job.header, sizeof(Header));
		put(qoi, (size_t)length);
		delete[] qoi;

		// written beside, then renamed: a picture is whole or absent
		char temp[64];
		static std::atomic<unsigned> counter(0);
		snprintf(temp, sizeof(temp), ".tmp-%d-%u", (int)getpid(), counter++);
		const std::string tempFile = dir + "/" + temp;
		const int fd = open(tempFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0)
			return;
		size_t done = 0;
		while (done < out.size())
		{
			const ssize_t n = ::write(fd, out.data() + done, out.size() - done);
			if (n <= 0)
				break;
			done += (size_t)n;
		}
		const bool whole = done == out.size();
		if (close(fd) != 0 || !whole || rename(tempFile.c_str(), file.c_str()) != 0)
			unlink(tempFile.c_str());
	}

	class Writer
	{
	public:
		Writer() : mExit(false), mThread(&Writer::run, this) { }
		~Writer()
		{
			{
				std::unique_lock<std::mutex> lock(mMutex);
				mExit = true;
				mJobs.clear(); // what waits is dropped: it is only a cache
			}
			mEvent.notify_all();
			if (mThread.joinable())
				mThread.join();
		}

		void add(Job&& job)
		{
			{
				std::unique_lock<std::mutex> lock(mMutex);
				if (mExit || mJobs.size() >= QUEUED_MAX)
					return;
				mJobs.push_back(std::move(job));
			}
			mEvent.notify_one();
		}

	private:
		void run()
		{
#if defined(__linux__)
			pthread_setname_np(pthread_self(), "picture cache");
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 19);
#endif
			while (true)
			{
				Job job;
				{
					std::unique_lock<std::mutex> lock(mMutex);
					mEvent.wait(lock, [this] { return mExit || !mJobs.empty(); });
					if (mExit)
						return;
					job = std::move(mJobs.front());
					mJobs.pop_front();
				}
				write(job);
			}
		}

		std::mutex mMutex;
		std::condition_variable mEvent;
		std::deque<Job> mJobs;
		bool mExit;
		std::thread mThread;
	};

	void store(const Key& key, const Picture& picture)
	{
		if (!enabled() || picture.rgba == nullptr || picture.width == 0 || picture.height == 0)
			return;

		static Writer writer;

		Job job;
		job.text = key.text();
		job.rgba.assign(picture.rgba, picture.rgba + picture.width * picture.height * 4);
		job.header.width = (uint32_t)picture.width;
		job.header.height = (uint32_t)picture.height;
		job.header.baseW = picture.baseSize.x();
		job.header.baseH = picture.baseSize.y();
		job.header.packedW = picture.packedSize.x();
		job.header.packedH = picture.packedSize.y();
		job.header.pixelsChecksum = checksum(job.rgba.data(), job.rgba.size());
		job.header.qoiLength = 0;
		writer.add(std::move(job));
	}
}
