// Win32 file access for store apps. The *FromApp functions work on the app's
// own folders and on locations the user granted access to.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace onboarding {

// Win32 error code, 0 on success.
using Win32Error = uint32_t;

bool IsDiskFull(Win32Error error);

class File {
 public:
  File() = default;
  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File();

  static File OpenRead(const std::filesystem::path& path, Win32Error* error);
  // Creates or truncates.
  static File Create(const std::filesystem::path& path, Win32Error* error);

  bool is_open() const { return handle_ != nullptr; }
  // Returns the number of bytes read, 0 at the end of the file.
  std::optional<size_t> Read(void* buffer, size_t size, Win32Error* error);
  bool Write(const void* data, size_t size, Win32Error* error);
  bool Flush(Win32Error* error);
  void Close();

 private:
  explicit File(void* handle) : handle_(handle) {}
  void* handle_ = nullptr;
};

struct FileStamp {
  uint64_t size;
  // FILETIME, 100 ns units.
  uint64_t last_write;
};

std::optional<FileStamp> StatFile(const std::filesystem::path& path);
bool CreateDirectories(const std::filesystem::path& path);
bool DeleteFileIfPresent(const std::filesystem::path& path);
// Replaces `to` if present.
bool MoveIntoPlace(const std::filesystem::path& from, const std::filesystem::path& to,
                   Win32Error* error);

}  // namespace onboarding
