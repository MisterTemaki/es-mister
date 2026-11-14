#pragma once

#include "Window.h"
#include <memory>
#include <atomic>

namespace httplib
{
	class Server;
}
class HttpServerWorker;

class HttpServerThread
{
public:
	HttpServerThread(Window * window);
	virtual ~HttpServerThread();

	static std::string getMimeType(const std::string &path);

private:
	Window*			mWindow;
	std::unique_ptr<HttpServerWorker> mWorker;
	void run(httplib::Server& server, const std::atomic<bool>& stopRequested);
};


