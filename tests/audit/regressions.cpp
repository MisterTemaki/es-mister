#include <curl/curl.h>
#include <map>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#define private public
#include "HttpReq.h"
#undef private
#include "utils/ZipFile.h"
#include "utils/FileSystemUtil.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
  try
  {
    require(argc >= 3, "usage: audit_regressions CASE ARG [DEST]");
    std::string mode = argv[1], arg = argv[2];
    if (mode == "header")
    {
      HttpReq request(arg);
      const std::string header = "X-Audit: bounded\r\n";
      std::unique_ptr<char[]> bytes(new char[header.size()]);
      std::memcpy(bytes.get(), header.data(), header.size());
      require(HttpReq::header_callback(bytes.get(), 1, header.size(), &request) == header.size(), "header byte count");
      require(request.getResponseHeader("X-Audit") == "bounded", "bounded header value");
    }
    else if (mode == "header-reset")
    {
      HttpReq request(arg);
      std::string header = "X-Redirect: old\r\n";
      HttpReq::header_callback(&header[0], 1, header.size(), &request);
      std::string status = "HTTP/1.1 200 OK\r\n";
      HttpReq::header_callback(&status[0], 1, status.size(), &request);
      require(request.getResponseHeader("X-Redirect").empty(), "redirect headers leaked into final response");
    }
    else if (mode == "custom-headers")
    {
      HttpReqOptions options;
      options.useCookieManager = false;
      options.customHeaders = {"X-Audit: one", "X-Other: two"};
      for (int i = 0; i < 20; ++i) { HttpReq request(arg, &options); }
    }
    else if (mode == "rename-fail")
    {
      require(!Utils::FileSystem::renameFile(arg, argv[3]), "rename must report failure");
      require(Utils::FileSystem::readAllText(argv[3]) == "original", "failed rename destroyed destination");
    }
    else if (mode == "rename-contracts")
    {
      require(!Utils::FileSystem::renameFile(arg + ".missing", argv[3]), "missing source reported success");
      require(!Utils::FileSystem::renameFile(arg, argv[3], false), "no-overwrite replaced destination");
      require(Utils::FileSystem::readAllText(argv[3]) == "original", "no-overwrite damaged destination");
      require(Utils::FileSystem::renameFile(arg, argv[3]), "replacement rename failed");
      require(Utils::FileSystem::readAllText(argv[3]) == "new-content", "replacement content mismatch");
    }
    else if (mode == "write-count")
    {
      HttpReq request(arg, std::string(argv[3]));
      char bytes[] = "abcdef";
      require(HttpReq::write_content(bytes, 2, 3, &request) == 6, "write callback must return bytes");
    }
    else if (mode == "download-preserve")
    {
      HttpReq request(arg, std::string(argv[3]));
      require(Utils::FileSystem::readAllText(argv[3]) == "original", "destination removed before transfer");
    }
    else if (mode == "flush-error")
    {
      HttpReq request(arg, std::string(argv[3]));
      std::fclose(request.mFile);
      request.mFile = std::fopen("/dev/full", "wb");
      require(request.mFile != nullptr, "open full device");
      char byte = 'x';
      HttpReq::write_content(&byte, 1, 1, &request);
      request.closeStream();
      require(request.mStatus == HttpReq::REQ_FILESTREAM_ERROR, "buffered flush error ignored");
    }
    else if (mode == "tls-trusted")
    {
      HttpReq request(arg, std::string(argv[4]));
      require(curl_easy_setopt(request.mHandle, CURLOPT_CAINFO, argv[3]) == CURLE_OK, "set test CA");
      require(request.wait(), "TLS request with trusted CA failed");
      require(Utils::FileSystem::readAllText(argv[4]) == "new-content", "trusted TLS content mismatch");
    }
    else if (mode == "download-ok" || mode == "download-fail" || mode == "tls-reject")
    {
      HttpReq request(arg, std::string(argv[3]));
      const bool success = request.wait();
      require(success == (mode == "download-ok"), "unexpected request result");
      require(Utils::FileSystem::readAllText(argv[3]) == (success ? "new-content" : "original"), "download damaged destination");
      if (success) require(request.getContent() == "new-content", "download content unavailable after commit");
    }
    else if (mode == "zip-list" || mode == "zip-reload" || mode == "zip-traversal" || mode == "zip-nested")
    {
      Utils::Zip::ZipFile zip;
      require(zip.load(arg), "load fixture ZIP");
      auto names = zip.namelist();
      require(!names.empty(), "ZIP names");
      if (mode == "zip-reload")
      {
        require(zip.load(argv[3]), "reload ZIP");
        require(zip.getFileMd5(u8"é.txt") == "900150983cd24fb0d6963f7d28e17f72", "stale filename map after reload");
      }
      else if (mode == "zip-traversal")
      {
        for (auto& name : names) require(!zip.extract(name, argv[3]), "unsafe archive entry accepted");
      }
      else if (mode == "zip-nested")
      {
        require(zip.extract(names.front(), argv[3]), "nested file without directory entry");
        require(Utils::FileSystem::readAllText(std::string(argv[3]) + "/nested/file.txt") == "abc", "nested content");
      }
    }
    else throw std::runtime_error("unknown case");
    std::cout << "PASS " << mode << '\n';
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
