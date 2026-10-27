// Game files under LocalFolder/gamedata, and the cache that lets later
// launches skip hashing files that did not change.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "catalog.h"
#include "file_io.h"

namespace onboarding {

enum class IngestResult {
  kOk,
  kBusy,
  kSizeMismatch,
  kBadHash,
  kDiskFull,
  kWriteError,
  // The upload or copy source failed or ended early.
  kSourceError,
};

const char* Describe(IngestResult result);

// Reads up to `capacity` bytes into `buffer`. Returns 0 at the end of the
// data, or nullopt when the source failed.
using ReadFunction = std::function<std::optional<size_t>(uint8_t* buffer, size_t capacity)>;

class FileStore {
 public:
  FileStore(std::filesystem::path root, Catalog& catalog);

  // ApplicationData::Current().LocalFolder() / "gamedata".
  static std::filesystem::path DefaultRoot();

  const std::filesystem::path& root() const { return root_; }

  // Sets every file to ok, missing or invalid. Hashes only files the cache
  // does not vouch for. Runs on a worker thread.
  void CheckAll();

  // Streams one file into place: written to a temporary file and hashed on
  // the way, then moved to its final path only if size and hash match.
  IngestResult Ingest(FileIndex index, const ReadFunction& read);

 private:
  std::filesystem::path FinalPath(const ManifestEntry& entry) const;
  std::filesystem::path TempPath(FileIndex index) const;
  std::optional<Sha256Digest> HashFile(FileIndex index, const std::filesystem::path& path);

  void LoadCache();
  void SaveCache();
  bool CacheVouches(const ManifestEntry& entry, const FileStamp& stamp);
  void Remember(const ManifestEntry& entry, std::optional<FileStamp> stamp);

  std::filesystem::path root_;
  Catalog& catalog_;
  std::mutex cache_mutex_;
  // Manifest path to the stamp of the file when it last verified.
  std::map<std::string, FileStamp> cache_;
};

}  // namespace onboarding
