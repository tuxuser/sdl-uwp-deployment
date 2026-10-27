#include "file_store.h"

#include <fstream>
#include <sstream>
#include <vector>

#include <winrt/windows.storage.h>

#include "sha256.h"

namespace onboarding {
namespace {

constexpr size_t kBufferSize = 1 << 20;
constexpr const char* kCacheFileName = ".verified";
constexpr const char* kIncomingDirName = ".incoming";

std::filesystem::path Utf8Path(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string HexDigest(const Sha256Digest& digest) {
  static const char kHex[] = "0123456789abcdef";
  std::string text;
  for (uint8_t byte : digest) {
    text += kHex[byte >> 4];
    text += kHex[byte & 15];
  }
  return text;
}

}  // namespace

const char* Describe(IngestResult result) {
  switch (result) {
    case IngestResult::kOk:
      return "ok";
    case IngestResult::kBusy:
      return "this file is already being written";
    case IngestResult::kSizeMismatch:
      return "size does not match the manifest";
    case IngestResult::kBadHash:
      return "content does not match the manifest (bad hash)";
    case IngestResult::kDiskFull:
      return "not enough storage space";
    case IngestResult::kWriteError:
      return "could not write the file";
    case IngestResult::kSourceError:
      return "the transfer was interrupted";
  }
  return "unknown error";
}

FileStore::FileStore(std::filesystem::path root, Catalog& catalog)
    : root_(std::move(root)), catalog_(catalog) {}

std::filesystem::path FileStore::DefaultRoot() {
  using winrt::Windows::Storage::ApplicationData;
  return std::filesystem::path(ApplicationData::Current().LocalFolder().Path().c_str()) /
         "gamedata";
}

std::filesystem::path FileStore::FinalPath(const ManifestEntry& entry) const {
  return root_ / Utf8Path(entry.path);
}

std::filesystem::path FileStore::TempPath(FileIndex index) const {
  return root_ / kIncomingDirName / (std::to_string(index) + ".part");
}

void FileStore::CheckAll() {
  catalog_.SetChecking(true);
  CreateDirectories(root_);
  LoadCache();
  const auto& entries = catalog_.manifest().entries();
  for (FileIndex i = 0; i < entries.size(); ++i) {
    const ManifestEntry& entry = entries[i];
    auto path = FinalPath(entry);
    auto stamp = StatFile(path);
    if (!stamp) {
      catalog_.SetStatus(i, FileStatus::kMissing);
      Remember(entry, std::nullopt);
      continue;
    }
    if (stamp->size != entry.size) {
      catalog_.SetStatus(i, FileStatus::kInvalid);
      Remember(entry, std::nullopt);
      continue;
    }
    if (CacheVouches(entry, *stamp)) {
      catalog_.SetStatus(i, FileStatus::kOk);
      continue;
    }
    auto digest = HashFile(i, path);
    bool ok = digest && *digest == entry.sha256;
    catalog_.SetStatus(i, ok ? FileStatus::kOk : FileStatus::kInvalid);
    Remember(entry, ok ? stamp : std::nullopt);
  }
  SaveCache();
  // Leftovers of transfers cut short by a crash or a closed app.
  std::error_code ignored;
  std::filesystem::remove_all(root_ / kIncomingDirName, ignored);
  catalog_.SetChecking(false);
}

std::optional<Sha256Digest> FileStore::HashFile(FileIndex index, const std::filesystem::path& path) {
  Win32Error error = 0;
  File file = File::OpenRead(path, &error);
  if (!file.is_open()) return std::nullopt;
  Sha256 sha;
  std::vector<uint8_t> buffer(kBufferSize);
  uint64_t done = 0;
  for (;;) {
    auto read = file.Read(buffer.data(), buffer.size(), &error);
    if (!read) return std::nullopt;
    if (*read == 0) break;
    sha.Update(buffer.data(), *read);
    done += *read;
    catalog_.SetProgress(index, FileStatus::kVerifying, done);
  }
  return sha.Finish();
}

IngestResult FileStore::Ingest(FileIndex index, const ReadFunction& read) {
  const ManifestEntry& entry = catalog_.manifest().entries().at(index);
  FileStatus previous = FileStatus::kMissing;
  if (!catalog_.TryClaim(index, &previous)) return IngestResult::kBusy;

  auto fail = [&](IngestResult result, const std::filesystem::path& temp) {
    DeleteFileIfPresent(temp);
    // The final file was not touched, so a good copy stays good.
    FileStatus status = previous == FileStatus::kOk ? FileStatus::kOk
                        : (result == IngestResult::kBadHash || result == IngestResult::kSizeMismatch)
                            ? FileStatus::kInvalid
                            : previous == FileStatus::kInvalid ? FileStatus::kInvalid
                                                               : FileStatus::kMissing;
    catalog_.SetStatus(index, status);
    if (result == IngestResult::kDiskFull) {
      catalog_.SetError("Not enough storage space (0x80070070). Free some space, then try again.");
    }
    return result;
  };

  auto temp = TempPath(index);
  auto final_path = FinalPath(entry);
  Win32Error error = 0;
  if (!CreateDirectories(temp.parent_path()) || !CreateDirectories(final_path.parent_path())) {
    return fail(IngestResult::kWriteError, temp);
  }
  File out = File::Create(temp, &error);
  if (!out.is_open()) {
    return fail(IsDiskFull(error) ? IngestResult::kDiskFull : IngestResult::kWriteError, temp);
  }

  Sha256 sha;
  std::vector<uint8_t> buffer(kBufferSize);
  uint64_t done = 0;
  for (;;) {
    auto got = read(buffer.data(), buffer.size());
    if (!got) {
      out.Close();
      return fail(IngestResult::kSourceError, temp);
    }
    if (*got == 0) break;
    done += *got;
    if (done > entry.size) {
      out.Close();
      return fail(IngestResult::kSizeMismatch, temp);
    }
    sha.Update(buffer.data(), *got);
    if (!out.Write(buffer.data(), *got, &error)) {
      out.Close();
      return fail(IsDiskFull(error) ? IngestResult::kDiskFull : IngestResult::kWriteError, temp);
    }
    catalog_.SetProgress(index, FileStatus::kTransferring, done);
  }
  catalog_.SetProgress(index, FileStatus::kVerifying, done);
  bool flushed = out.Flush(&error);
  out.Close();
  if (!flushed) {
    return fail(IsDiskFull(error) ? IngestResult::kDiskFull : IngestResult::kWriteError, temp);
  }
  if (done != entry.size) return fail(IngestResult::kSizeMismatch, temp);
  if (sha.Finish() != entry.sha256) return fail(IngestResult::kBadHash, temp);
  if (!MoveIntoPlace(temp, final_path, &error)) {
    // The old copy may already be gone, or may still be intact if removing it
    // failed.
    if (!StatFile(final_path)) previous = FileStatus::kMissing;
    return fail(IngestResult::kWriteError, temp);
  }
  Remember(entry, StatFile(final_path));
  SaveCache();
  catalog_.SetStatus(index, FileStatus::kOk);
  return IngestResult::kOk;
}

void FileStore::LoadCache() {
  std::lock_guard lock(cache_mutex_);
  cache_.clear();
  std::ifstream in(root_ / kCacheFileName);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string digest;
    FileStamp stamp{};
    if (!(fields >> digest >> stamp.size >> stamp.last_write)) continue;
    std::string path;
    std::getline(fields >> std::ws, path);
    // The digest is part of the key so a changed manifest never trusts an old
    // file of the same name and size.
    cache_[digest + " " + path] = stamp;
  }
}

void FileStore::SaveCache() {
  std::lock_guard lock(cache_mutex_);
  auto path = root_ / kCacheFileName;
  auto temp = root_ / (std::string(kCacheFileName) + ".tmp");
  {
    std::ofstream out(temp, std::ios::trunc);
    for (const auto& [key, stamp] : cache_) {
      size_t space = key.find(' ');
      out << key.substr(0, space) << ' ' << stamp.size << ' ' << stamp.last_write << ' '
          << key.substr(space + 1) << '\n';
    }
    if (!out) return;
  }
  MoveIntoPlace(temp, path, nullptr);
}

bool FileStore::CacheVouches(const ManifestEntry& entry, const FileStamp& stamp) {
  std::lock_guard lock(cache_mutex_);
  auto it = cache_.find(HexDigest(entry.sha256) + " " + entry.path);
  return it != cache_.end() && it->second.size == stamp.size &&
         it->second.last_write == stamp.last_write;
}

void FileStore::Remember(const ManifestEntry& entry, std::optional<FileStamp> stamp) {
  std::lock_guard lock(cache_mutex_);
  auto key = HexDigest(entry.sha256) + " " + entry.path;
  if (stamp) {
    cache_[key] = *stamp;
  } else {
    cache_.erase(key);
  }
}

}  // namespace onboarding
