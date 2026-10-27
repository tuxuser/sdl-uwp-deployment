// The list of game files the app needs, embedded at build time from
// assets/manifest.txt.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace onboarding {

using Sha256Digest = std::array<uint8_t, 32>;

struct ManifestEntry {
  Sha256Digest sha256;
  uint64_t size;
  // Relative path with '/' separators.
  std::string path;
};

// Index of an entry in Manifest::entries().
using FileIndex = size_t;

class Manifest {
 public:
  // Parses lines of "<sha256-hex> <size-bytes> <relative/path>". Malformed
  // lines are a build error of the data, so parsing throws.
  static Manifest Parse(std::string_view text);
  static const Manifest& Embedded();

  const std::vector<ManifestEntry>& entries() const { return entries_; }
  uint64_t total_bytes() const { return total_bytes_; }

  std::optional<FileIndex> Find(std::string_view path) const;

  // Matches a path as sent by a browser or found under a picked folder. The
  // path may carry extra leading folders, or only the file name, as long as
  // the remaining suffix names exactly one entry.
  std::optional<FileIndex> Match(std::string_view path) const;

 private:
  std::vector<ManifestEntry> entries_;
  uint64_t total_bytes_ = 0;
};

// True for a relative path made of plain components: no "..", ".", empty
// components, drive letters, backslashes or control characters.
bool IsSafeRelativePath(std::string_view path);

}  // namespace onboarding
