#include "file_io.h"

#include <windows.h>
#include <fileapifromapp.h>

namespace onboarding {
namespace {

void SetError(Win32Error* error, Win32Error value) {
  if (error) *error = value;
}

}  // namespace

bool IsDiskFull(Win32Error error) {
  return error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL;
}

File::File(File&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

File::~File() { Close(); }

File File::OpenRead(const std::filesystem::path& path, Win32Error* error) {
  HANDLE handle = CreateFile2FromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    SetError(error, GetLastError());
    return File();
  }
  SetError(error, 0);
  return File(handle);
}

File File::Create(const std::filesystem::path& path, Win32Error* error) {
  HANDLE handle = CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    SetError(error, GetLastError());
    return File();
  }
  SetError(error, 0);
  return File(handle);
}

std::optional<size_t> File::Read(void* buffer, size_t size, Win32Error* error) {
  DWORD read = 0;
  DWORD chunk = static_cast<DWORD>(size > 0x40000000 ? 0x40000000 : size);
  if (!ReadFile(handle_, buffer, chunk, &read, nullptr)) {
    SetError(error, GetLastError());
    return std::nullopt;
  }
  SetError(error, 0);
  return read;
}

bool File::Write(const void* data, size_t size, Win32Error* error) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (size > 0) {
    DWORD chunk = static_cast<DWORD>(size > 0x40000000 ? 0x40000000 : size);
    DWORD written = 0;
    if (!WriteFile(handle_, bytes, chunk, &written, nullptr)) {
      SetError(error, GetLastError());
      return false;
    }
    bytes += written;
    size -= written;
  }
  SetError(error, 0);
  return true;
}

bool File::Flush(Win32Error* error) {
  if (!FlushFileBuffers(handle_)) {
    SetError(error, GetLastError());
    return false;
  }
  SetError(error, 0);
  return true;
}

void File::Close() {
  if (handle_) {
    CloseHandle(handle_);
    handle_ = nullptr;
  }
}

std::optional<FileStamp> StatFile(const std::filesystem::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &data) ||
      (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    return std::nullopt;
  }
  return FileStamp{
      (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
      (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime};
}

bool CreateDirectories(const std::filesystem::path& path) {
  if (path.empty()) return true;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &data)) {
    return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  }
  if (path.has_parent_path() && path.parent_path() != path && !CreateDirectories(path.parent_path())) {
    return false;
  }
  return CreateDirectoryFromAppW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool DeleteFileIfPresent(const std::filesystem::path& path) {
  if (DeleteFileFromAppW(path.c_str())) return true;
  DWORD error = GetLastError();
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool MoveIntoPlace(const std::filesystem::path& from, const std::filesystem::path& to,
                   Win32Error* error) {
  // MoveFileFromApp does not overwrite. Removing first can leave no file at
  // `to` after a crash, but never a partial one.
  if (!DeleteFileIfPresent(to) || !MoveFileFromAppW(from.c_str(), to.c_str())) {
    SetError(error, GetLastError());
    return false;
  }
  SetError(error, 0);
  return true;
}

}  // namespace onboarding
