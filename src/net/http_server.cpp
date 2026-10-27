#include "http_server.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <thread>
#include <vector>

#include <winrt/windows.foundation.h>
#include <winrt/windows.storage.streams.h>

#include "web_page.h"

namespace onboarding {
namespace {

using winrt::Windows::Networking::Sockets::StreamSocket;
using winrt::Windows::Networking::Sockets::StreamSocketListener;
using winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InputStreamOptions;

constexpr size_t kMaxHeaderBytes = 16 * 1024;
constexpr uint32_t kReadChunk = 256 * 1024;
// A peer that sends nothing for this long is treated as gone, so a phone that
// left the network does not hold its file or upload slot forever.
constexpr std::chrono::seconds kReadTimeout{30};
constexpr std::chrono::seconds kSlotTimeout{120};
// Overall limits, so a client that trickles bytes cannot hold a connection or
// an upload slot indefinitely.
constexpr std::chrono::seconds kHeaderDeadline{10};
constexpr std::chrono::seconds kBodyBaseTime{60};
constexpr uint64_t kMinBytesPerSecond = 32 * 1024;
// Rejected uploads are read to the end up to this size, so the browser gets
// the answer instead of a connection reset.
constexpr uint64_t kMaxDrainBytes = 256ull * 1024 * 1024;

struct Request {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;  // lower-case names
  std::string body_prefix;                     // body bytes read with the headers
};

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::string Trim(std::string_view text) {
  size_t start = text.find_first_not_of(" \t");
  size_t end = text.find_last_not_of(" \t");
  return start == std::string_view::npos ? std::string() : std::string(text.substr(start, end - start + 1));
}

class Connection {
 public:
  explicit Connection(StreamSocket socket)
      : socket_(std::move(socket)), reader_(socket_.InputStream()), writer_(socket_.OutputStream()) {
    reader_.InputStreamOptions(InputStreamOptions::Partial);
  }

  // Returns at most `max` bytes, empty when the peer closed the connection or
  // sent nothing within the read timeout.
  std::string ReadSome(uint32_t max, std::chrono::milliseconds timeout = kReadTimeout) {
    auto load = reader_.LoadAsync(max);
    if (load.wait_for(timeout) != winrt::Windows::Foundation::AsyncStatus::Completed) {
      load.Cancel();
      return {};
    }
    uint32_t loaded = load.GetResults();
    std::string data(loaded, '\0');
    if (loaded > 0) {
      reader_.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t*>(data.data()),
                                                   reinterpret_cast<uint8_t*>(data.data()) + loaded));
    }
    return data;
  }

  std::optional<Request> ReadRequest() {
    std::string data;
    size_t header_end;
    const auto deadline = std::chrono::steady_clock::now() + kHeaderDeadline;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (data.size() > kMaxHeaderBytes || left.count() <= 0) return std::nullopt;
      std::string chunk = ReadSome(4096, left);
      if (chunk.empty()) return std::nullopt;
      data += chunk;
      // A TLS handshake: the browser upgraded the address to https. Closing
      // at once lets it fall back to http instead of waiting for a timeout.
      if (static_cast<uint8_t>(data[0]) == 0x16) return std::nullopt;
    }
    Request request;
    request.body_prefix = data.substr(header_end + 4);
    std::string_view head(data.data(), header_end);
    size_t line_end = head.find("\r\n");
    std::string_view request_line = head.substr(0, line_end);
    size_t sp1 = request_line.find(' ');
    size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) return std::nullopt;
    request.method = std::string(request_line.substr(0, sp1));
    request.target = std::string(request_line.substr(sp1 + 1, sp2 - sp1 - 1));
    while (line_end != std::string_view::npos) {
      size_t start = line_end + 2;
      line_end = head.find("\r\n", start);
      std::string_view line = head.substr(start, line_end == std::string_view::npos ? line_end : line_end - start);
      size_t colon = line.find(':');
      if (colon == std::string_view::npos) continue;
      auto [it, inserted] =
          request.headers.emplace(Lower(std::string(line.substr(0, colon))), Trim(line.substr(colon + 1)));
      // Two lengths for one body cannot both be honored.
      if (!inserted && it->first == "content-length") return std::nullopt;
    }
    return request;
  }

  void Send(int code, std::string_view reason, std::string_view content_type, std::string_view body) {
    std::string head = "HTTP/1.1 " + std::to_string(code) + " " + std::string(reason) +
                       "\r\nContent-Type: " + std::string(content_type) +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    auto bytes = [](std::string_view text) {
      return winrt::array_view<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()),
                                              reinterpret_cast<const uint8_t*>(text.data()) + text.size());
    };
    writer_.WriteBytes(bytes(head));
    if (!body.empty()) writer_.WriteBytes(bytes(body));
    writer_.StoreAsync().get();
    writer_.FlushAsync().get();
  }

  void Drain(uint64_t bytes) {
    while (bytes > 0 && bytes <= kMaxDrainBytes) {
      std::string chunk = ReadSome(static_cast<uint32_t>(std::min<uint64_t>(bytes, kReadChunk)));
      if (chunk.empty()) return;
      bytes -= std::min<uint64_t>(bytes, chunk.size());
    }
  }

  void SendJson(int code, std::string_view reason, std::string_view message) {
    std::string body = "{\"result\":\"";
    for (char c : message) {
      if (c == '"' || c == '\\') body += '\\';
      body += c;
    }
    body += "\"}";
    Send(code, reason, "application/json", body);
  }

 private:
  StreamSocket socket_;
  DataReader reader_;
  DataWriter writer_;
};

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          out += escaped;
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

}  // namespace

bool UrlDecode(std::string_view in, std::string* out) {
  out->clear();
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (c == '%') {
      if (i + 2 >= in.size()) return false;
      auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
      };
      int hi = hex(in[i + 1]);
      int lo = hex(in[i + 2]);
      if (hi < 0 || lo < 0) return false;
      *out += static_cast<char>(hi << 4 | lo);
      i += 2;
    } else {
      *out += c;
    }
  }
  return true;
}

std::string StatusJson(const Snapshot& snapshot) {
  std::string json = "{\"files\":[";
  for (size_t i = 0; i < snapshot.files.size(); ++i) {
    const FileView& file = snapshot.files[i];
    if (i) json += ',';
    json += "{\"path\":" + JsonString(file.entry->path) +
            ",\"size\":" + std::to_string(file.entry->size) +
            ",\"state\":" + JsonString(StatusName(file.status)) +
            ",\"done\":" + std::to_string(file.done) + "}";
  }
  json += "],\"ready\":" + std::to_string(snapshot.ready_count) +
          ",\"count\":" + std::to_string(snapshot.files.size()) +
          ",\"bytes_ready\":" + std::to_string(snapshot.ready_bytes) +
          ",\"bytes_total\":" + std::to_string(snapshot.total_bytes) +
          ",\"checking\":" + (snapshot.checking ? "true" : "false") +
          ",\"error\":" + JsonString(snapshot.error) + "}";
  return json;
}

HttpServer::HttpServer(FileStore& store, Catalog& catalog) : store_(store), catalog_(catalog) {}

bool HttpServer::Start(uint16_t port, std::string* error) {
  listener_.Control().KeepAlive(false);
  listener_.ConnectionReceived(
      [this](const StreamSocketListener&, const StreamSocketListenerConnectionReceivedEventArgs& args) {
        // A thread per connection: an upload can take minutes and must not
        // hold up /status requests.
        if (connections_.fetch_add(1) >= kMaxConnections) {
          connections_.fetch_sub(1);
          return;  // Dropping the socket closes it.
        }
        std::thread([this, socket = args.Socket()]() mutable {
          winrt::init_apartment(winrt::apartment_type::multi_threaded);
          try {
            HandleConnection(std::move(socket));
          } catch (...) {
            // A bad request must never take the app down.
          }
          connections_.fetch_sub(1);
        }).detach();
      });
  try {
    listener_.BindServiceNameAsync(winrt::to_hstring(port)).get();
  } catch (const winrt::hresult_error& e) {
    if (error) *error = winrt::to_string(e.message());
    return false;
  }
  return true;
}

void HttpServer::HandleConnection(StreamSocket socket) {
  try {
    Connection connection(std::move(socket));
    auto request = connection.ReadRequest();
    if (!request) return;
    std::string_view target = request->target;
    if (size_t query = target.find('?'); query != std::string_view::npos) target = target.substr(0, query);

    if (request->method == "GET" && (target == "/" || target == "/index.html")) {
      connection.Send(200, "OK", "text/html; charset=utf-8",
                      std::string_view(reinterpret_cast<const char*>(kWebPage), sizeof(kWebPage)));
      return;
    }
    if (request->method == "GET" && target == "/status") {
      connection.Send(200, "OK", "application/json", StatusJson(catalog_.Take()));
      return;
    }
    constexpr std::string_view kUploadPrefix = "/upload/";
    if (request->method != "PUT" || target.substr(0, kUploadPrefix.size()) != kUploadPrefix) {
      connection.SendJson(404, "Not Found", "unknown request");
      return;
    }

    std::string path;
    if (!UrlDecode(target.substr(kUploadPrefix.size()), &path) || !IsSafeRelativePath(path)) {
      connection.SendJson(400, "Bad Request", "invalid path");
      return;
    }
    auto index = catalog_.manifest().Match(path);
    if (!index) {
      if (auto cl = request->headers.find("content-length"); cl != request->headers.end()) {
        uint64_t n = 0;
        std::from_chars(cl->second.data(), cl->second.data() + cl->second.size(), n);
        connection.Drain(n - std::min<uint64_t>(n, request->body_prefix.size()));
      }
      connection.SendJson(404, "Not Found", "not in the manifest");
      return;
    }
    const ManifestEntry& entry = catalog_.manifest().entries()[*index];
    if (request->headers.count("transfer-encoding")) {
      connection.SendJson(501, "Not Implemented", "send the file with Content-Length");
      return;
    }
    auto length_header = request->headers.find("content-length");
    uint64_t length = 0;
    if (length_header == request->headers.end() ||
        std::from_chars(length_header->second.data(), length_header->second.data() + length_header->second.size(),
                        length).ec != std::errc() ||
        length_header->second.find_first_not_of("0123456789") != std::string::npos) {
      connection.SendJson(411, "Length Required", "a valid Content-Length is required");
      return;
    }
    if (length != entry.size) {
      connection.Drain(length - std::min<uint64_t>(length, request->body_prefix.size()));
      connection.SendJson(413, "Payload Too Large", "size does not match the manifest");
      return;
    }
    if (catalog_.Take().checking) {
      connection.Drain(length - std::min<uint64_t>(length, request->body_prefix.size()));
      connection.SendJson(503, "Service Unavailable", "the console is still checking files, try again shortly");
      return;
    }

    if (FileStatus status = catalog_.Take().files[*index].status;
        status == FileStatus::kTransferring || status == FileStatus::kVerifying) {
      connection.Drain(length - std::min<uint64_t>(length, request->body_prefix.size()));
      connection.SendJson(409, "Conflict", Describe(IngestResult::kBusy));
      return;
    }
    if (!upload_slots_.try_acquire_for(kSlotTimeout)) {
      connection.Drain(length - std::min<uint64_t>(length, request->body_prefix.size()));
      connection.SendJson(503, "Service Unavailable", "too many uploads, try again shortly");
      return;
    }
    struct Release {
      std::counting_semaphore<2>& slots;
      ~Release() { slots.release(); }
    } release{upload_slots_};

    std::string pending = std::move(request->body_prefix);
    uint64_t remaining = entry.size;
    const auto body_deadline = std::chrono::steady_clock::now() + kBodyBaseTime +
                               std::chrono::seconds(entry.size / kMinBytesPerSecond);
    auto read = [&](uint8_t* out, size_t capacity) -> std::optional<size_t> {
      if (remaining == 0) return size_t{0};
      if (std::chrono::steady_clock::now() > body_deadline) return std::nullopt;
      if (pending.empty()) {
        try {
          pending = connection.ReadSome(static_cast<uint32_t>(std::min<uint64_t>(remaining, kReadChunk)));
        } catch (const winrt::hresult_error&) {
          return std::nullopt;
        }
        // The browser went away before sending the whole file.
        if (pending.empty()) return std::nullopt;
      }
      size_t n = static_cast<size_t>(std::min<uint64_t>({pending.size(), capacity, remaining}));
      std::memcpy(out, pending.data(), n);
      pending.erase(0, n);
      remaining -= n;
      return n;
    };

    IngestResult result = store_.Ingest(*index, read);
    switch (result) {
      case IngestResult::kOk:
        connection.SendJson(200, "OK", "ok");
        break;
      case IngestResult::kBusy:
        connection.Drain(remaining - std::min<uint64_t>(remaining, pending.size()));
        connection.SendJson(409, "Conflict", Describe(result));
        break;
      case IngestResult::kSizeMismatch:
        connection.SendJson(413, "Payload Too Large", Describe(result));
        break;
      case IngestResult::kBadHash:
        connection.SendJson(422, "Unprocessable Entity", Describe(result));
        break;
      case IngestResult::kDiskFull:
        connection.SendJson(507, "Insufficient Storage", Describe(result));
        break;
      case IngestResult::kWriteError:
        connection.SendJson(500, "Internal Server Error", Describe(result));
        break;
      case IngestResult::kSourceError:
        // The connection is gone; nothing to answer.
        break;
    }
  } catch (const winrt::hresult_error&) {
    // Dropped connection; the file state was already reset by Ingest.
  }
}

}  // namespace onboarding
