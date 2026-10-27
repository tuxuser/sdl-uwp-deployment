// State of every manifest file, shared between the worker threads that check,
// upload or copy files and the render thread that draws it.
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "manifest/manifest.h"

namespace onboarding {

enum class FileStatus {
  kUnchecked,
  kMissing,
  // Bytes are arriving through an upload or a copy.
  kTransferring,
  kVerifying,
  kOk,
  // Present but with the wrong size or hash.
  kInvalid,
};

const char* StatusName(FileStatus status);

struct FileView {
  const ManifestEntry* entry;
  FileStatus status;
  // Bytes received or hashed so far in the current operation.
  uint64_t done;
};

struct Snapshot {
  std::vector<FileView> files;
  size_t ready_count = 0;
  uint64_t ready_bytes = 0;
  uint64_t total_bytes = 0;
  bool checking = false;
  // Index of the most recently active file, if any operation is running.
  std::optional<FileIndex> current;
  std::string error;

  bool busy() const { return checking || current.has_value(); }
  bool all_ready() const { return ready_count == files.size(); }
};

class Catalog {
 public:
  explicit Catalog(const Manifest& manifest);

  const Manifest& manifest() const { return manifest_; }

  // Claims a file for writing. Fails while another transfer of the same file
  // runs.
  bool TryClaim(FileIndex index, FileStatus* previous);
  void SetStatus(FileIndex index, FileStatus status);
  void SetProgress(FileIndex index, FileStatus status, uint64_t done);
  void SetChecking(bool checking);

  void SetError(std::string message);
  void ClearError();

  Snapshot Take() const;

 private:
  struct Slot {
    FileStatus status = FileStatus::kUnchecked;
    uint64_t done = 0;
  };

  const Manifest& manifest_;
  mutable std::mutex mutex_;
  std::vector<Slot> slots_;
  // True from the start so no upload slips in before the first check runs.
  bool checking_ = true;
  std::optional<FileIndex> current_;
  std::string error_;
};

}  // namespace onboarding
