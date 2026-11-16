#include "scrapers/LibretroThumbnailsScraper.h"
#include "scrapers/LibretroArt.h"

#include "FileData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

namespace
{
	const char* const NAME = "Libretro-thumbnails";

	std::vector<std::string> namesOf(SystemData* system)
	{
		std::vector<std::string> names;
		if (system == nullptr)
			return names;
		names.push_back(system->getName());
		for (auto id : system->getPlatformIds())
		{
			const std::string platform = PlatformIds::getPlatformName(id);
			if (!platform.empty())
				names.push_back(platform);
		}
		return names;
	}

	std::string joined(const std::vector<std::string>& urls)
	{
		std::string out;
		for (const std::string& u : urls)
		{
			if (u.empty() || ("\t" + out + "\t").find("\t" + u + "\t") != std::string::npos)
				continue;
			if (!out.empty())
				out += '\t';
			out += u;
		}
		return out;
	}

	// One game: waits for its system's art to be loaded, then picks its images.
	class LibretroThumbnailsRequest : public ScraperRequest
	{
	public:
		LibretroThumbnailsRequest(std::vector<ScraperSearchResult>& results, const LibretroArt::System& system, const ScraperSearchParams& params)
			: ScraperRequest(results), mSystem(system), mMetadata(params.game->getMetadata())
		{
			setStatus(ASYNC_IN_PROGRESS);
			mSource = LibretroArt::Source::get(system);
			mPath = params.game->getPath();
			mName = params.nameOverride.empty() ? Utils::FileSystem::getStem(mPath) : params.nameOverride;
			// a typed name is looked for as it is; the systems with a No-Intro DAT are cartridges (discs: not read)
			mReadCrc = params.nameOverride.empty() && mSystem.dat;
		}

		void update() override
		{
			if (mStatus != ASYNC_IN_PROGRESS || !mSource->poll())
				return;

			const LibretroArt::Art& art = mSource->art();
			LibretroArt::RomCrc crc;
			if (mReadCrc && !art.byCrc.empty())
				crc = LibretroArt::romCrc(mPath);

			const std::string region = Settings::getInstance()->getString("ScraperRegion");
			const LibretroArt::Choice c = LibretroArt::choose(art, mName, crc, region);

			// each kind's address, then what stands in for it
			std::vector<std::string> urls[LibretroArt::KINDS];
			for (int k = 0; k < LibretroArt::KINDS; k++)
			{
				if (!c.names[k].empty())
					urls[k].push_back(LibretroArt::artUrl(art.repo, (LibretroArt::Kind)k, c.names[k]));
				for (const std::string& g : c.guesses)
					urls[k].push_back(LibretroArt::artUrl(art.repo, (LibretroArt::Kind)k, g));
			}
			std::vector<std::string> box = urls[LibretroArt::BOXART];
			box.insert(box.end(), c.db.begin(), c.db.end());
			const std::vector<std::string>& snap = urls[LibretroArt::SNAP];
			const std::vector<std::string>& title = urls[LibretroArt::TITLE];

			LOG(LogInfo) << NAME << ": " << mName << (c.noIntro.empty() ? "" : " = " + c.noIntro + " (CRC32)")
				<< " -> box art: " << (c.names[0].empty() ? (c.db.empty() ? "-" : "database") : c.names[0])
				<< ", screenshot: " << (c.names[1].empty() ? "-" : c.names[1]) << ", title: " << (c.names[2].empty() ? "-" : c.names[2])
				<< (c.guesses.empty() ? "" : " (not listed: " + std::to_string(c.guesses.size()) + " names tried)");

			ScraperSearchResult result(NAME);
			result.mdl = mMetadata; // only images come from here: the rest stays as ES has it
			if (result.mdl.getName().empty())
				result.mdl.set(MetaDataId::Name, Utils::FileSystem::getStem(mPath));

			auto concat = [](std::vector<std::string> a, const std::vector<std::string>& b, const std::vector<std::string>& c2)
			{
				a.insert(a.end(), b.begin(), b.end());
				a.insert(a.end(), c2.begin(), c2.end());
				return a;
			};

			// the image: the kind asked for, then the others (a title screen is better than nothing)
			const std::string imageSource = Settings::getInstance()->getString("ScrapperImageSrc");
			std::string image;
			if (imageSource == "box-2D")
				image = joined(concat(box, title, snap));
			else if (imageSource == "ss")
				image = joined(concat(snap, title, box));
			else if (imageSource == "sstitle")
				image = joined(concat(title, snap, box));
			if (!image.empty())
				result.urls[MetaDataId::Image] = ScraperSearchItem(image, ".png");

			// the box: a box art only
			const std::string thumbSource = Settings::getInstance()->getString("ScrapperThumbSrc");
			if (thumbSource == "box-2D" && imageSource != "box-2D" && !box.empty())
				result.urls[MetaDataId::Thumbnail] = ScraperSearchItem(joined(box), ".png");

			if (Settings::getInstance()->getBool("ScrapeTitleShot") && !title.empty())
				result.urls[MetaDataId::TitleShot] = ScraperSearchItem(joined(title), ".png");

			if (result.hasMedia())
				mResults.push_back(result);

			setStatus(ASYNC_DONE);
		}

	private:
		LibretroArt::System mSystem;
		LibretroArt::Source* mSource;
		MetaDataList mMetadata;
		std::string mPath;
		std::string mName;
		bool mReadCrc;
	};
}

void LibretroThumbnailsScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests, std::vector<ScraperSearchResult>& results)
{
	if (params.game == nullptr)
		return;

	SystemData* system = params.system != nullptr ? params.system : params.game->getSourceFileData()->getSystem();
	const LibretroArt::System* s = LibretroArt::findSystem(namesOf(system), Utils::FileSystem::getExtension(params.game->getPath()));
	if (s == nullptr)
		return;

	requests.push(std::unique_ptr<ScraperRequest>(new LibretroThumbnailsRequest(results, *s, params)));
}

bool LibretroThumbnailsScraper::isSupportedPlatform(SystemData* system)
{
	return LibretroArt::findSystem(namesOf(system), "") != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& LibretroThumbnailsScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds = {
		ScraperMediaSource::Box2d,
		ScraperMediaSource::Screenshot,
		ScraperMediaSource::TitleShot,
		ScraperMediaSource::Region
	};

	return mdds;
}
