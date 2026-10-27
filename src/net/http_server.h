// Minimal HTTP/1.1 server for uploading game files from a browser on the LAN.
//
//   GET  /                     upload page
//   GET  /status               JSON with per-file state and totals
//   PUT  /upload/<path>        request body is the file, path is URL-encoded
//
// One request per connection (Connection: close), no TLS. Only reachable on
// the local network, as allowed by the privateNetworkClientServer capability.
#pragma once

#include <atomic>
#include <cstdint>
#include <semaphore>
#include <string>

#include <winrt/windows.networking.sockets.h>

#include "storage/file_store.h"

namespace onboarding {

class HttpServer {
 public:
  HttpServer(FileStore& store, Catalog& catalog);

  // Waits for the bind to finish, so it must not run on the UI thread.
  bool Start(uint16_t port, std::string* error);

 private:
  void HandleConnection(winrt::Windows::Networking::Sockets::StreamSocket socket);

  FileStore& store_;
  Catalog& catalog_;
  winrt::Windows::Networking::Sockets::StreamSocketListener listener_;
  // Two uploads at a time keep the disk busy without starving /status.
  std::counting_semaphore<2> upload_slots_{2};
  // Each connection has its own thread; this caps idle or stalled ones.
  static constexpr int kMaxConnections = 32;
  std::atomic<int> connections_{0};
};

// URL-decodes a path component. Returns false on bad escapes.
bool UrlDecode(std::string_view in, std::string* out);

std::string StatusJson(const Snapshot& snapshot);

}  // namespace onboarding
