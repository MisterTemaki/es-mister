#pragma once
#ifndef ES_APP_SCRAPERS_LIBRETRO_THUMBNAILS_SCRAPER_H
#define ES_APP_SCRAPERS_LIBRETRO_THUMBNAILS_SCRAPER_H

#include "scrapers/Scraper.h"

// Box arts, screenshots and title screens from libretro-thumbnails, and for the games it has no box art of, the
// cover an online database has (Hasheous/IGDB, TheGamesDB): see LibretroArt.h. No account and no key. Only images:
// the game's name and the rest of what ES knows of it stay as they are.
class LibretroThumbnailsScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;
	const std::set<ScraperMediaSource>& getSupportedMedias() override;

	// two games at a time: the images come from GitHub's servers, the ARM resizes them
	int getThreadCount(std::string& result) override { return 2; }
};

#endif // ES_APP_SCRAPERS_LIBRETRO_THUMBNAILS_SCRAPER_H
