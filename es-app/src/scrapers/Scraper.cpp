#include "scrapers/Scraper.h"

#include "FileData.h"
#include "ArcadeDBJSONScraper.h"
#include "GamesDBJSONScraper.h"
#include "ScreenScraper.h"
#include "Log.h"
#include "Settings.h"
#include "SystemData.h"
#ifdef ES_USE_STB_IMAGE
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>
#else
#include <FreeImage.h>
#endif
#include <fstream>
#include <cstring>
#include <vector>
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <thread>
#include <SDL_timer.h>
#include "HfsDBScraper.h"
#include "IGDBScraper.h"
#include "LibretroThumbnailsScraper.h"
#include "utils/Uri.h"

#define OVERQUOTA_RETRY_DELAY 15000
#define OVERQUOTA_RETRY_COUNT 5

std::vector<std::pair<std::string, Scraper*>> Scraper::scrapers
{
#ifdef SCREENSCRAPER_DEV_LOGIN
	{ "ScreenScraper", new ScreenScraperScraper() },
#endif

#ifdef GAMESDB_APIKEY
	{ "TheGamesDB", new TheGamesDBScraper() },
#endif

#ifdef HFS_DEV_LOGIN
	{ "HfsDB", new HfsDBScraper() },
#endif

	{ "IGDB", new IGDBScraper() },
	{ "ArcadeDB", new ArcadeDBScraper() },
	{ "Libretro-thumbnails", new LibretroThumbnailsScraper() }
};

std::string Scraper::getScraperName(Scraper* scraper)
{
	for (auto engine : scrapers)
		if (scraper == engine.second)
			return engine.first;

	return std::string();
}

int Scraper::getScraperIndex(const std::string& name)
{
	for (int i = 0 ; i < scrapers.size() ; i++)
		if (name == scrapers[i].first)
			return i;

	return -1;
}

std::string Scraper::getScraperNameFromIndex(int index)
{
	if (index >= 0 && index < scrapers.size())
		return scrapers[index].first;

	return std::string();
}

Scraper* Scraper::getScraper(const std::string name)
{	
	auto scraper = name;
	if(scraper.empty())
		scraper = Settings::getInstance()->getString("Scraper");
	
	for (auto scrap : Scraper::scrapers)
		if (scrap.first == scraper)
			return scrap.second;

	return nullptr;
}

bool Scraper::isValidConfiguredScraper()
{
	return getScraper() != nullptr;
}

bool Scraper::hasAnyMedia(FileData* file)
{
	if (isMediaSupported(ScraperMediaSource::Screenshot) || isMediaSupported(ScraperMediaSource::Box2d) || isMediaSupported(ScraperMediaSource::Box3d) || isMediaSupported(ScraperMediaSource::Mix) || isMediaSupported(ScraperMediaSource::TitleShot) || isMediaSupported(ScraperMediaSource::FanArt))
		if (!Settings::getInstance()->getString("ScrapperImageSrc").empty() && !file->getMetadata(MetaDataId::Image).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Image)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Box2d) || isMediaSupported(ScraperMediaSource::Box3d))
		if (!Settings::getInstance()->getString("ScrapperThumbSrc").empty() && !file->getMetadata(MetaDataId::Thumbnail).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Thumbnail)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Wheel) || isMediaSupported(ScraperMediaSource::Marquee))
		if (!Settings::getInstance()->getString("ScrapperLogoSrc").empty() && !file->getMetadata(MetaDataId::Marquee).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Marquee)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Manual))
		if (Settings::getInstance()->getBool("ScrapeManual") && !file->getMetadata(MetaDataId::Manual).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Manual)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Map))
		if (Settings::getInstance()->getBool("ScrapeMap") && !file->getMetadata(MetaDataId::Map).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Map)))
			return true;

	if (isMediaSupported(ScraperMediaSource::FanArt))
		if (Settings::getInstance()->getBool("ScrapeFanart") && !file->getMetadata(MetaDataId::FanArt).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::FanArt)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Video))
		if (Settings::getInstance()->getBool("ScrapeVideos") && !file->getMetadata(MetaDataId::Video).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Video)))
			return true;

	if (isMediaSupported(ScraperMediaSource::BoxBack))
		if (Settings::getInstance()->getBool("ScrapeBoxBack") && !file->getMetadata(MetaDataId::BoxBack).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::BoxBack)))
			return true;

	if (isMediaSupported(ScraperMediaSource::TitleShot))
		if (Settings::getInstance()->getBool("ScrapeTitleShot") && !file->getMetadata(MetaDataId::TitleShot).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::TitleShot)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Cartridge))
		if (Settings::getInstance()->getBool("ScrapeCartridge") && !file->getMetadata(MetaDataId::Cartridge).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Cartridge)))
			return true;

	if (isMediaSupported(ScraperMediaSource::Bezel_16_9))
		if (Settings::getInstance()->getBool("ScrapeBezel") && !file->getMetadata(MetaDataId::Bezel).empty() && Utils::FileSystem::exists(file->getMetadata(MetaDataId::Bezel)))
			return true;
	
	return false;
}

bool Scraper::hasMissingMedia(FileData* file)
{
	if (isMediaSupported(ScraperMediaSource::Screenshot) || isMediaSupported(ScraperMediaSource::Box2d) || isMediaSupported(ScraperMediaSource::Box3d) || isMediaSupported(ScraperMediaSource::Mix) || isMediaSupported(ScraperMediaSource::TitleShot) || isMediaSupported(ScraperMediaSource::FanArt))
		if (!Settings::getInstance()->getString("ScrapperImageSrc").empty() && (file->getMetadata(MetaDataId::Image).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Image))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Box2d) || isMediaSupported(ScraperMediaSource::Box3d))
		if (!Settings::getInstance()->getString("ScrapperThumbSrc").empty() && (file->getMetadata(MetaDataId::Thumbnail).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Thumbnail))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Wheel) || isMediaSupported(ScraperMediaSource::Marquee))
		if (!Settings::getInstance()->getString("ScrapperLogoSrc").empty() && (file->getMetadata(MetaDataId::Marquee).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Marquee))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Manual))
		if (Settings::getInstance()->getBool("ScrapeManual") && (file->getMetadata(MetaDataId::Manual).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Manual))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Map))
		if (Settings::getInstance()->getBool("ScrapeMap") && (file->getMetadata(MetaDataId::Map).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Map))))
			return true;

	if (isMediaSupported(ScraperMediaSource::FanArt))
		if (Settings::getInstance()->getBool("ScrapeFanart") && (file->getMetadata(MetaDataId::FanArt).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::FanArt))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Video))
		if (Settings::getInstance()->getBool("ScrapeVideos") && (file->getMetadata(MetaDataId::Video).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Video))))
			return true;

	if (isMediaSupported(ScraperMediaSource::BoxBack))
		if (Settings::getInstance()->getBool("ScrapeBoxBack") && (file->getMetadata(MetaDataId::BoxBack).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::BoxBack))))
			return true;

	if (isMediaSupported(ScraperMediaSource::TitleShot))
		if (Settings::getInstance()->getBool("ScrapeTitleShot") && (file->getMetadata(MetaDataId::TitleShot).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::TitleShot))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Cartridge))
		if (Settings::getInstance()->getBool("ScrapeCartridge") && (file->getMetadata(MetaDataId::Cartridge).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Cartridge))))
			return true;

	if (isMediaSupported(ScraperMediaSource::Bezel_16_9))
		if (Settings::getInstance()->getBool("ScrapeBezel") && (file->getMetadata(MetaDataId::Bezel).empty() || !Utils::FileSystem::exists(file->getMetadata(MetaDataId::Bezel))))
			return true;
	

	return false;
}

bool Scraper::isMediaSupported(const Scraper::ScraperMediaSource& md)
{
	auto mdds = getSupportedMedias();
	return mdds.find(md) != mdds.cend();
}

std::unique_ptr<ScraperSearchHandle> Scraper::search(const ScraperSearchParams& params)
{
	std::unique_ptr<ScraperSearchHandle> handle(new ScraperSearchHandle());
	generateRequests(params, handle->mRequestQueue, handle->mResults);
	return handle;
}

std::vector<std::string> Scraper::getScraperList()
{
	std::vector<std::string> list;
	for(auto& it : Scraper::scrapers)
		list.push_back(it.first);

	return list;
}

// ScraperSearchHandle
ScraperSearchHandle::ScraperSearchHandle()
{
	setStatus(ASYNC_IN_PROGRESS);
}

void ScraperSearchHandle::update()
{
	if(mStatus == ASYNC_DONE)
		return;

	if(!mRequestQueue.empty())
	{
		// a request can add more requests to the queue while running,
		// so be careful with references into the queue
		auto& req = *(mRequestQueue.front());
		AsyncHandleStatus status = req.status();

		if(status == ASYNC_ERROR)
		{
			// propagate error
			setError(req.getErrorCode(), Utils::String::removeHtmlTags(req.getStatusString()));

			// empty our queue
			while(!mRequestQueue.empty())
				mRequestQueue.pop();

			return;
		}

		// finished this one, see if we have any more
		if (status == ASYNC_DONE)
		{
			// If we have results, exit, else process the next request
			if (mResults.size() > 0)
			{
				while (!mRequestQueue.empty())
					mRequestQueue.pop();
			}
			else
				mRequestQueue.pop();
		}
	}

	// we finished without any errors!
	if(mRequestQueue.empty() && mStatus != ASYNC_ERROR)
	{
		setStatus(ASYNC_DONE);
		return;
	}
}

// ScraperHttpRequest
ScraperHttpRequest::ScraperHttpRequest(std::vector<ScraperSearchResult>& resultsWrite, const std::string& url, HttpReqOptions* options)
	: ScraperRequest(resultsWrite)
{
	setStatus(ASYNC_IN_PROGRESS);

	if (options != nullptr)
		mOptions = *options;
	
	mRequest = new HttpReq(url, &mOptions);
	mUrl = url;
	mRetryCount = 0;
	mOverQuotaPendingTime = 0;
	mOverQuotaRetryDelay = OVERQUOTA_RETRY_DELAY;
	mOverQuotaRetryCount = OVERQUOTA_RETRY_COUNT;
}

ScraperHttpRequest::~ScraperHttpRequest()
{
	delete mRequest;	
}

std::string ScraperHttpRequest::getDependencyResponse(const std::string& id) const
{
	auto it = mResponses.find(id);
	if (it == mResponses.cend())
		return "";

	return it->second;
}

void ScraperHttpRequest::update()
{
	if (mOverQuotaPendingTime > 0)
	{
		int lastTime = SDL_GetTicks();
		if (lastTime - mOverQuotaPendingTime > mOverQuotaRetryDelay)
		{
			mOverQuotaPendingTime = 0;

			LOG(LogDebug) << "REQ_429_TOOMANYREQUESTS : Retrying";

			std::string url = mRequest->getUrl();
			delete mRequest;
			mRequest = new HttpReq(url, &mOptions);
		}

		return;
	}

	HttpReq::Status status = mRequest->status();

	// not ready yet
	if (status == HttpReq::REQ_IN_PROGRESS)
		return;

	if (status == HttpReq::REQ_SUCCESS)
	{
		std::string body = mRequest->getContent();
		mResponses[mDependencyId] = body;

		if (mDependencyId.empty())
			preProcess(body);

		if (!mDependencyQueue.empty())
		{
			status = HttpReq::REQ_IN_PROGRESS;
				
			auto item = mDependencyQueue.front();
			mDependencyQueue.pop();

			mDependencyId = item.first;
			std::string url = item.second;	
			delete mRequest;
			mRequest = new HttpReq(url, &mOptions);
			return;
		}

		setStatus(ASYNC_DONE); // if process() has an error, status will be changed to ASYNC_ERROR
		process(mResponses[""], mResults);
		return;
	}

	if (status == HttpReq::REQ_429_TOOMANYREQUESTS)
	{
		mRetryCount++;
		if (mRetryCount >= mOverQuotaRetryCount)
		{
			setStatus(ASYNC_DONE); // Ignore error
			return;
		}


		auto retryDelay = mRequest->getResponseHeader("Retry-After");
		if (!retryDelay.empty())
		{
			mOverQuotaRetryCount = 1;
			mOverQuotaRetryDelay = Utils::String::toInteger(retryDelay) * 1000;

			if (!retryOn249() && mOverQuotaRetryDelay > 5000)
			{
				setStatus(ASYNC_DONE); // Ignore error if delay > 5 seconds
				return;
			}
		}

		setStatus(ASYNC_IN_PROGRESS);

		mOverQuotaPendingTime = SDL_GetTicks();
		LOG(LogDebug) << "REQ_429_TOOMANYREQUESTS : Retrying in " << mOverQuotaRetryDelay << " seconds";
		return;
	}

	// Ignored errors
	if (status == HttpReq::REQ_404_NOTFOUND || status == HttpReq::REQ_IO_ERROR)
	{
		setStatus(ASYNC_DONE);
		return;
	}

	// Blocking errors
	if (status != HttpReq::REQ_SUCCESS)
	{		
		setError(status, Utils::String::removeHtmlTags(mRequest->getErrorMsg()));
		return;
	}	

	// everything else is some sort of error
	LOG(LogError) << "ScraperHttpRequest network error (status: " << status << ") - " << mRequest->getErrorMsg();
	setError(Utils::String::removeHtmlTags(mRequest->getErrorMsg()));
}

std::unique_ptr<MDResolveHandle> ScraperSearchResult::resolveMetaDataAssets(const ScraperSearchParams& search)
{
	return std::unique_ptr<MDResolveHandle>(new MDResolveHandle(*this, search));
}

// metadata resolving stuff
MDResolveHandle::MDResolveHandle(const ScraperSearchResult& result, const ScraperSearchParams& search) : mResult(result)
{
	mPercent = -1;

	bool overWriteMedias = Settings::getInstance()->getBool("ScrapeOverWrite") && search.overWriteMedias;

	for (auto& url : result.urls)
	{
		if (url.second.url.empty())
			continue;

		if (!overWriteMedias && Utils::FileSystem::exists(search.game->getMetadata(url.first)))
		{
			mResult.mdl.set(url.first, search.game->getMetadata(url.first));
			if (mResult.urls.find(url.first) != mResult.urls.cend())
				mResult.urls[url.first].url = "";

			continue;
		}

		bool resize = true;

		std::string suffix = "image";
		switch (url.first)
		{
		case MetaDataId::Video: suffix = "video";  resize = false; break;
		case MetaDataId::FanArt: suffix = "fanart"; resize = false; break;
		case MetaDataId::BoxBack: suffix = "boxback"; resize = false; break;
		case MetaDataId::BoxArt: suffix = "box"; resize = false; break;
		case MetaDataId::Wheel: suffix = "wheel"; resize = false; break;		
		case MetaDataId::TitleShot: suffix = "titleshot"; break;
		case MetaDataId::Manual: suffix = "manual"; resize = false;  break;
		case MetaDataId::Magazine: suffix = "magazine"; resize = false;  break;
		case MetaDataId::Map: suffix = "map"; resize = false; break;
		case MetaDataId::Cartridge: suffix = "cartridge"; break;
		case MetaDataId::Bezel: suffix = "bezel"; resize = false; break;
		}

		auto ext = url.second.format;
		if (ext.empty())
			ext = Utils::FileSystem::getExtension(url.second.url);

		std::string resourcePath = Scraper::getSaveAsPath(search.game, url.first, ext);

		if (!overWriteMedias && Utils::FileSystem::exists(resourcePath))
		{
			mResult.mdl.set(url.first, resourcePath);
			if (mResult.urls.find(url.first) != mResult.urls.cend())
				mResult.urls[url.first].url = "";

			continue;
		}

		mFuncs.push_back(new ResolvePair(
			[this, url, resourcePath, resize] 
			{ 
				return downloadImageAsync(url.second.url, resourcePath, resize); 
			},
			[this, url](ImageDownloadHandle* result)
			{
				auto finalFile = result->getImageFileName();

				if (Utils::FileSystem::getFileSize(finalFile) > 0)
					mResult.mdl.set(url.first, finalFile);

				if (mResult.urls.find(url.first) != mResult.urls.cend())
					mResult.urls[url.first].url = "";
			},
			suffix, result.mdl.getName()));
	}

	auto it = mFuncs.cbegin();
	if (it == mFuncs.cend())
		setStatus(ASYNC_DONE);
	else
	{
		mSource = (*it)->source;
		mCurrentItem = (*it)->name;
		(*it)->Run();		
	}
}

void MDResolveHandle::update()
{
	if(mStatus == ASYNC_DONE || mStatus == ASYNC_ERROR)
		return;
	
	auto it = mFuncs.cbegin();
	if (it == mFuncs.cend())
	{
		setStatus(ASYNC_DONE);
		return;
	}

	ResolvePair* pPair = (*it);
		
	if (pPair->handle->status() == ASYNC_IN_PROGRESS)
		mPercent = pPair->handle->getPercent();

	if (pPair->handle->status() == ASYNC_ERROR)
	{
		setError(pPair->handle->getErrorCode(), pPair->handle->getStatusString());
		for (auto fc : mFuncs)
			delete fc;

		return;
	}
	else if (pPair->handle->status() == ASYNC_DONE)
	{		
		pPair->onFinished(pPair->handle.get());
		mFuncs.erase(it);
		delete pPair;

		auto next = mFuncs.cbegin();
		if (next != mFuncs.cend())
		{
			mSource = (*next)->source;
			mCurrentItem = (*next)->name;
			(*next)->Run();
		}
	}
	
	if(mFuncs.empty())
		setStatus(ASYNC_DONE);
}

std::unique_ptr<ImageDownloadHandle> MDResolveHandle::downloadImageAsync(const std::string& url, const std::string& saveAs, bool resize)
{
	LOG(LogDebug) << "downloadImageAsync : " << url << " -> " << saveAs;

	return std::unique_ptr<ImageDownloadHandle>(new ImageDownloadHandle(url, saveAs, 
		resize ? Settings::getInstance()->getInt("ScraperResizeWidth") : 0,
		resize ? Settings::getInstance()->getInt("ScraperResizeHeight") : 0));
}

ImageDownloadHandle::ImageDownloadHandle(const std::string& url, const std::string& path, int maxWidth, int maxHeight) : 
	mSavePath(path), mMaxWidth(maxWidth), mMaxHeight(maxHeight)
{
	mRetryCount = 0;
	mOverQuotaPendingTime = 0;
	mOverQuotaRetryDelay = OVERQUOTA_RETRY_DELAY;
	mOverQuotaRetryCount = OVERQUOTA_RETRY_COUNT;

	// several addresses, tab apart: each tried in turn until one gives the image
	mUrls = Utils::String::split(url, '\t', true);
	if (mUrls.empty())
		mUrls.push_back(url);
	mUrlIndex = 0;
	mLinkHops = 0;
	mRequest = nullptr;
	startRequest(mUrls[0]);
}

void ImageDownloadHandle::startRequest(const std::string& url)
{
	delete mRequest;

	HttpReqOptions options;
	options.outputFilename = mSavePath;

	if (url.find("screenscraper") != std::string::npos && url.find("/medias/") != std::string::npos)
	{
		auto splits = Utils::String::split(url, '/', true);
		if (splits.size() > 1)
			options.customHeaders.push_back("Referer: https://" + splits[1] + "/gameinfos.php?gameid=" + splits[splits.size() - 2] + "&action=onglet&zone=gameinfosmedias");
	}

	if (url.find("screenscraper") != std::string::npos && (mSavePath.find(".jpg") != std::string::npos || mSavePath.find(".png") != std::string::npos) && url.find("media=map") == std::string::npos)
	{
		Utils::Uri uri(url);

		if (mMaxWidth > 0)
		{
			uri.arguments.set("maxwidth", std::to_string(mMaxWidth));
			uri.arguments.set("maxheight", std::to_string(mMaxWidth));
		}
		else if (mMaxHeight > 0)
		{
			uri.arguments.set("maxwidth", std::to_string(mMaxHeight));
			uri.arguments.set("maxheight", std::to_string(mMaxHeight));
		}

		mRequest = new HttpReq(uri.toString(), &options);
	}
	else
		mRequest = new HttpReq(url, &options);
}

bool ImageDownloadHandle::tryNextUrl()
{
	if (mUrlIndex + 1 >= (int)mUrls.size())
		return false;

	mUrlIndex++;
	mLinkHops = 0;
	mRetryCount = 0;
	LOG(LogDebug) << "ImageDownloadHandle : trying " << mUrls[mUrlIndex];
	startRequest(mUrls[mUrlIndex]);
	return true;
}

// ".png", ".jpg", ".gif" or ".webp" by a file's first bytes, or ""
static std::string sniffImageExtension(const std::string& path)
{
	unsigned char head[12] = {};
	FILE* f = fopen(path.c_str(), "rb");
	if (f == nullptr)
		return "";
	const size_t n = fread(head, 1, sizeof(head), f);
	fclose(f);

	if (n >= 8 && memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0)
		return ".png";
	if (n >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
		return ".jpg";
	if (n >= 6 && (memcmp(head, "GIF87a", 6) == 0 || memcmp(head, "GIF89a", 6) == 0))
		return ".gif";
	if (n >= 12 && memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WEBP", 4) == 0)
		return ".webp";
	return "";
}

// A symbolic link of a libretro-thumbnails repository: GitHub's raw server answers with the name of the file it points
// to (a few bytes of text), not with the image. The address of that file, or "".
static std::string libretroLinkTarget(const std::string& url, const std::string& file)
{
	if (url.find("raw.githubusercontent.com/libretro-thumbnails/") == std::string::npos || Utils::FileSystem::getFileSize(file) > 512)
		return "";

	std::string target = Utils::String::trim(Utils::FileSystem::readAllText(file));
	if (target.empty() || target.find('\n') != std::string::npos || Utils::String::toLower(Utils::FileSystem::getExtension(target)) != ".png")
		return "";

	std::string base = url.substr(0, url.rfind('/'));
	for (auto part : Utils::String::split(target, '/', true))
	{
		if (part == ".")
			continue;
		if (part == "..")
			base = base.substr(0, base.rfind('/'));
		else
			base += "/" + HttpReq::urlEncode(part);
	}
	return base;
}

ImageDownloadHandle::~ImageDownloadHandle()
{
	delete mRequest;
}

int ImageDownloadHandle::getPercent()
{
	if (mRequest->status() == HttpReq::REQ_IN_PROGRESS)
		return mRequest->getPercent();

	return -1;
}

void ImageDownloadHandle::update()
{
	if (mOverQuotaPendingTime > 0)
	{
		int lastTime = SDL_GetTicks();
		if (lastTime - mOverQuotaPendingTime > mOverQuotaRetryDelay)
		{
			mOverQuotaPendingTime = 0;

			LOG(LogDebug) << "REQ_429_TOOMANYREQUESTS : Retrying";

			std::string url = mRequest->getUrl();
			delete mRequest;
			mRequest = new HttpReq(url, mSavePath);
		}

		return;
	}

	HttpReq::Status status = mRequest->status();

	if (status == HttpReq::REQ_IN_PROGRESS)
		return;
	
	if (status == HttpReq::REQ_429_TOOMANYREQUESTS)
	{
		mRetryCount++;
		if (mRetryCount >= mOverQuotaRetryCount)
		{
			setStatus(ASYNC_DONE); // Ignore error
			return;
		}

		setStatus(ASYNC_IN_PROGRESS);

		auto retryDelay = mRequest->getResponseHeader("Retry-After");
		if (!retryDelay.empty())
		{
			mOverQuotaRetryCount = 1;
			mOverQuotaRetryDelay = Utils::String::toInteger(retryDelay) * 1000;
		}

		mOverQuotaPendingTime = SDL_GetTicks();
		LOG(LogDebug) << "REQ_429_TOOMANYREQUESTS : Retrying in " << mOverQuotaRetryDelay << " seconds";
		return;
	}

	// Another address to try: any error sends to it
	if (status != HttpReq::REQ_SUCCESS && tryNextUrl())
		return;

	// Ignored errors
	if (status == HttpReq::REQ_404_NOTFOUND || status == HttpReq::REQ_IO_ERROR)
	{
		setStatus(ASYNC_DONE);
		return;
	}

	// Blocking errors
	if (status != HttpReq::REQ_SUCCESS)
	{
		setError(status, Utils::String::removeHtmlTags(mRequest->getErrorMsg()));
		return;
	}

	if (status == HttpReq::REQ_SUCCESS && mStatus == ASYNC_IN_PROGRESS)
	{
		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(mSavePath));

		// Make sure extension is the good one, according to the response 'Content-Type' (without its parameters:
		// "image/jpeg; x-api-version=1")
		std::string contentType = Utils::String::toLower(mRequest->getResponseHeader("Content-Type"));
		if (contentType.find(';') != std::string::npos)
			contentType = Utils::String::trim(contentType.substr(0, contentType.find(';')));

		// Text where a picture was asked for: a libretro-thumbnails link (its target is fetched), else an error page
		if (Utils::String::startsWith(contentType, "text/"))
		{
			std::string target = libretroLinkTarget(mRequest->getUrl(), mSavePath);
			Utils::FileSystem::removeFile(mSavePath);

			if (!target.empty() && mLinkHops < 4)
			{
				mLinkHops++;
				LOG(LogDebug) << "ImageDownloadHandle : link to " << target;
				startRequest(target);
				return;
			}

			if (tryNextUrl())
				return;

			setStatus(ASYNC_DONE);
			return;
		}

		// what the file is, by its first bytes (some servers name a PNG image/jpeg), else by what the server says
		std::string trueExtension = sniffImageExtension(mSavePath);
		// a WebP picture: ES's picture reader (stb) has no WebP, so the cover would stay empty ("Could not
		// initialize texture"); the next address is tried instead (Hasheous serves some IGDB covers as WebP)
		if (trueExtension == ".webp")
		{
			Utils::FileSystem::removeFile(mSavePath);
			if (tryNextUrl())
				return;
			setStatus(ASYNC_DONE);
			return;
		}
		if (!trueExtension.empty() && trueExtension != ext)
		{
			auto newFileName = Utils::FileSystem::changeExtension(mSavePath, trueExtension);
			if (Utils::FileSystem::renameFile(mSavePath, newFileName))
			{
				mSavePath = newFileName;
				ext = trueExtension;
			}
		}
		else if (trueExtension.empty() && !contentType.empty())
		{
			if (Utils::String::startsWith(contentType, "image/"))
			{
				trueExtension = "." + contentType.substr(6);
				if (trueExtension == ".jpeg")
					trueExtension = ".jpg";
				else if (trueExtension == ".svg+xml")
					trueExtension = ".svg";

			}
			else if (Utils::String::startsWith(contentType, "video/"))
			{
				trueExtension = "." + contentType.substr(6);
				if (trueExtension == ".quicktime")
					trueExtension = ".mov";
			}

			if (!trueExtension.empty() && trueExtension != ext)
			{
				auto newFileName = Utils::FileSystem::changeExtension(mSavePath, trueExtension);
				if (Utils::FileSystem::renameFile(mSavePath, newFileName))
				{
					mSavePath = newFileName;
					ext = trueExtension;
				}
			}
		}

		// It's an image ?
		if (mSavePath.find("-fanart") == std::string::npos && mSavePath.find("-bezel") == std::string::npos && mSavePath.find("-map") == std::string::npos && (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".gif"))
		{
			try { resizeImage(mSavePath, mMaxWidth, mMaxHeight); }
			catch(...) { }
		}
	}

	setStatus(ASYNC_DONE);
}

//you can pass 0 for width or height to keep aspect ratio
#ifdef ES_USE_STB_IMAGE
bool resizeImage(const std::string& path, int maxWidth, int maxHeight)
{
	// nothing to do
	if(maxWidth == 0 && maxHeight == 0)
		return true;

	unsigned char header[8] = {};
	{
		std::ifstream probe(path, std::ios::binary);
		probe.read((char*)header, sizeof(header));
	}
	const bool png = memcmp(header, "\x89PNG", 4) == 0;
	const bool jpeg = header[0] == 0xFF && header[1] == 0xD8;
	if (!png && !jpeg)
		return true; // stb writes PNG and JPEG only: anything else is kept as it is

	int width = 0, height = 0, components = 0;
	unsigned char* image = stbi_load(path.c_str(), &width, &height, &components, png ? 4 : 3);
	if (image == nullptr)
	{
		LOG(LogError) << "Error - could not read image \"" << path << "\"!";
		return false;
	}

	if (width == 0 || height == 0)
	{
		stbi_image_free(image);
		return true;
	}

	if(maxWidth == 0)
		maxWidth = (int)((maxHeight / (float)height) * width);
	else if(maxHeight == 0)
		maxHeight = (int)((maxWidth / (float)width) * height);

	if (width <= maxWidth && height <= maxHeight)
	{
		stbi_image_free(image);
		return true;
	}

	const int channels = png ? 4 : 3;
	std::vector<unsigned char> scaled((size_t)maxWidth * maxHeight * channels);
	const bool resized = stbir_resize(image, width, height, 0, scaled.data(), maxWidth, maxHeight, 0, png ? STBIR_RGBA : STBIR_RGB, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_TRIANGLE) != nullptr;
	stbi_image_free(image);

	if (!resized)
	{
		LOG(LogError) << "Could not resize image! (not enough memory?)";
		return false;
	}

	const bool saved = png ? stbi_write_png(path.c_str(), maxWidth, maxHeight, channels, scaled.data(), maxWidth * channels) != 0
		: stbi_write_jpg(path.c_str(), maxWidth, maxHeight, channels, scaled.data(), 90) != 0;

	if(!saved)
		LOG(LogError) << "Failed to save resized image!";

	return saved;
}
#else
bool resizeImage(const std::string& path, int maxWidth, int maxHeight)
{
	// nothing to do
	if(maxWidth == 0 && maxHeight == 0)
		return true;

	FREE_IMAGE_FORMAT format = FIF_UNKNOWN;
	FIBITMAP* image = NULL;
	
	//detect the filetype
#if WIN32
	format = FreeImage_GetFileTypeU(Utils::String::convertToWideString(path).c_str(), 0);
	if(format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilenameU(Utils::String::convertToWideString(path).c_str());
#else
	format = FreeImage_GetFileType(path.c_str(), 0);
	if(format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilename(path.c_str());
#endif
	if(format == FIF_UNKNOWN)
	{
		LOG(LogError) << "Error - could not detect filetype for image \"" << path << "\"!";
		return false;
	}

	//make sure we can read this filetype first, then load it
	if(FreeImage_FIFSupportsReading(format))
	{
#if WIN32
		image = FreeImage_LoadU(format, Utils::String::convertToWideString(path).c_str());
#else
		image = FreeImage_Load(format, path.c_str());
#endif
	}else{
		LOG(LogError) << "Error - file format reading not supported for image \"" << path << "\"!";
		return false;
	}

	float width = (float)FreeImage_GetWidth(image);
	float height = (float)FreeImage_GetHeight(image);

	if (width == 0 || height == 0)
	{
		FreeImage_Unload(image);
		return true;
	}

	if(maxWidth == 0)
		maxWidth = (int)((maxHeight / height) * width);
	else if(maxHeight == 0)
		maxHeight = (int)((maxWidth / width) * height);
	
	if (width <= maxWidth && height <= maxHeight)
	{
		FreeImage_Unload(image);
		return true;
	}
		
	FIBITMAP* imageRescaled = FreeImage_Rescale(image, maxWidth, maxHeight, FILTER_BILINEAR);
	FreeImage_Unload(image);

	if(imageRescaled == NULL)
	{
		LOG(LogError) << "Could not resize image! (not enough memory? invalid bitdepth?)";
		return false;
	}

	bool saved = false;
	
	try
	{
#if WIN32
		saved = (FreeImage_SaveU(format, imageRescaled, Utils::String::convertToWideString(path).c_str()) != 0);
#else
		saved = (FreeImage_Save(format, imageRescaled, path.c_str()) != 0);
#endif
	}
	catch(...) { }

	FreeImage_Unload(imageRescaled);

	if(!saved)
		LOG(LogError) << "Failed to save resized image!";

	return saved;
}
#endif // ES_USE_STB_IMAGE

std::string Scraper::getSaveAsPath(FileData* game, const MetaDataId metadataId, const std::string& extension)
{
	std::string suffix = "image";
	std::string folder = "images";

	switch (metadataId)
	{
	case MetaDataId::Thumbnail: suffix = "thumb"; break;
	case MetaDataId::Marquee: suffix = "marquee"; break;
	case MetaDataId::Video: suffix = "video"; folder = "videos"; break;
	case MetaDataId::FanArt: suffix = "fanart"; break;
	case MetaDataId::BoxBack: suffix = "boxback"; break;
	case MetaDataId::BoxArt: suffix = "box"; break;
	case MetaDataId::Wheel: suffix = "wheel"; break;
	case MetaDataId::TitleShot: suffix = "titleshot"; break;
	case MetaDataId::Manual: suffix = "manual"; folder = "manuals";  break;
	case MetaDataId::Magazine: suffix = "magazine"; folder = "magazines"; break;
	case MetaDataId::Map: suffix = "map"; break;
	case MetaDataId::Cartridge: suffix = "cartridge"; break;
	case MetaDataId::Bezel: suffix = "bezel"; break;
	}

	auto system = game->getSourceFileData()->getSystem();

	const std::string subdirectory = system->getName();
	const std::string name = Utils::FileSystem::getStem(game->getPath()) + "-" + suffix;

	std::string path = system->getRootFolder()->getPath() + "/" + folder + "/";

	if(!Utils::FileSystem::exists(path))
		Utils::FileSystem::createDirectory(path);

	path += name + extension;
	return path;
}
