#include "manifest.h"

#include <charconv>
#include <stdexcept>

#include "embedded_manifest.h"

namespace onboarding {
namespace {

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::optional<Sha256Digest> ParseDigest(std::string_view hex) {
  if (hex.size() != 64) return std::nullopt;
  Sha256Digest digest{};
  for (size_t i = 0; i < digest.size(); ++i) {
    int hi = HexValue(hex[i * 2]);
    int lo = HexValue(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    digest[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return digest;
}

char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Windows file names are case-insensitive, so matching is too.
bool EqualsNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
  }
  return true;
}

// True when `path` is `suffix` or ends with "/" + `suffix`.
bool HasPathSuffix(std::string_view path, std::string_view suffix) {
  if (path.size() == suffix.size()) return EqualsNoCase(path, suffix);
  if (path.size() < suffix.size() + 1) return false;
  size_t start = path.size() - suffix.size();
  return path[start - 1] == '/' && EqualsNoCase(path.substr(start), suffix);
}

}  // namespace

Manifest Manifest::Parse(std::string_view text) {
  Manifest manifest;
  size_t line_number = 0;
  while (!text.empty()) {
    size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;

    size_t first = line.find(' ');
    size_t second = first == std::string_view::npos ? first : line.find(' ', first + 1);
    if (second == std::string_view::npos) {
      throw std::runtime_error("manifest line " + std::to_string(line_number) + ": expected 3 fields");
    }
    auto digest = ParseDigest(line.substr(0, first));
    std::string size_text(line.substr(first + 1, second - first - 1));
    std::string_view path = line.substr(second + 1);
    if (!digest || size_text.empty() ||
        size_text.find_first_not_of("0123456789") != std::string::npos ||
        !IsSafeRelativePath(path)) {
      throw std::runtime_error("manifest line " + std::to_string(line_number) + ": invalid field");
    }
    if (manifest.Find(path)) {
      // File names are case-insensitive, so these would share one file.
      throw std::runtime_error("manifest line " + std::to_string(line_number) + ": duplicate path");
    }
    uint64_t size = 0;
    if (std::from_chars(size_text.data(), size_text.data() + size_text.size(), size).ec != std::errc()) {
      throw std::runtime_error("manifest line " + std::to_string(line_number) + ": invalid size");
    }
    manifest.entries_.push_back(ManifestEntry{*digest, size, std::string(path)});
    manifest.total_bytes_ += size;
  }
  return manifest;
}

const Manifest& Manifest::Embedded() {
  static const Manifest manifest = Parse(kEmbeddedManifest);
  return manifest;
}

std::optional<FileIndex> Manifest::Find(std::string_view path) const {
  for (FileIndex i = 0; i < entries_.size(); ++i) {
    if (EqualsNoCase(entries_[i].path, path)) return i;
  }
  return std::nullopt;
}

std::optional<FileIndex> Manifest::Match(std::string_view path) const {
  if (auto exact = Find(path)) return exact;
  // Drop leading folders one at a time; the first suffix that names exactly
  // one entry wins. Ambiguous suffixes are not guessed.
  std::string_view suffix = path;
  while (!suffix.empty()) {
    std::optional<FileIndex> found;
    bool ambiguous = false;
    for (FileIndex i = 0; i < entries_.size(); ++i) {
      if (HasPathSuffix(entries_[i].path, suffix)) {
        if (found) ambiguous = true;
        found = i;
      }
    }
    if (found && !ambiguous) return found;
    size_t slash = suffix.find('/');
    if (slash == std::string_view::npos) break;
    suffix.remove_prefix(slash + 1);
  }
  return std::nullopt;
}

bool IsSafeRelativePath(std::string_view path) {
  if (path.empty() || path.size() > 1024) return false;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    std::string_view part = path.substr(start, end == std::string_view::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    for (char c : part) {
      unsigned char u = static_cast<unsigned char>(c);
      if (u < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
          c == '>' || c == '|') {
        return false;
      }
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return true;
}

}  // namespace onboarding
