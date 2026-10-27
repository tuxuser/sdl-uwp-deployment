#include "catalog.h"

namespace onboarding {
namespace {

bool IsActive(FileStatus status) {
  return status == FileStatus::kTransferring || status == FileStatus::kVerifying;
}

}  // namespace

const char* StatusName(FileStatus status) {
  switch (status) {
    case FileStatus::kUnchecked:
      return "unchecked";
    case FileStatus::kMissing:
      return "missing";
    case FileStatus::kTransferring:
      return "uploading";
    case FileStatus::kVerifying:
      return "verifying";
    case FileStatus::kOk:
      return "ok";
    case FileStatus::kInvalid:
      return "bad-hash";
  }
  return "unknown";
}

Catalog::Catalog(const Manifest& manifest)
    : manifest_(manifest), slots_(manifest.entries().size()) {}

bool Catalog::TryClaim(FileIndex index, FileStatus* previous) {
  std::lock_guard lock(mutex_);
  Slot& slot = slots_.at(index);
  if (IsActive(slot.status) || checking_) return false;
  if (previous) *previous = slot.status;
  slot.status = FileStatus::kTransferring;
  slot.done = 0;
  current_ = index;
  return true;
}

void Catalog::SetStatus(FileIndex index, FileStatus status) { SetProgress(index, status, 0); }

void Catalog::SetProgress(FileIndex index, FileStatus status, uint64_t done) {
  std::lock_guard lock(mutex_);
  Slot& slot = slots_.at(index);
  slot.status = status;
  slot.done = done;
  if (IsActive(status)) {
    current_ = index;
  } else if (current_ == index) {
    current_.reset();
    // Another transfer may still run; show that one instead.
    for (FileIndex i = 0; i < slots_.size(); ++i) {
      if (IsActive(slots_[i].status)) current_ = i;
    }
  }
}

void Catalog::SetChecking(bool checking) {
  std::lock_guard lock(mutex_);
  checking_ = checking;
}

void Catalog::SetError(std::string message) {
  std::lock_guard lock(mutex_);
  error_ = std::move(message);
}

void Catalog::ClearError() {
  std::lock_guard lock(mutex_);
  error_.clear();
}

Snapshot Catalog::Take() const {
  std::lock_guard lock(mutex_);
  Snapshot snapshot;
  const auto& entries = manifest_.entries();
  snapshot.files.reserve(entries.size());
  for (FileIndex i = 0; i < entries.size(); ++i) {
    snapshot.files.push_back(FileView{&entries[i], slots_[i].status, slots_[i].done});
    if (slots_[i].status == FileStatus::kOk) {
      ++snapshot.ready_count;
      snapshot.ready_bytes += entries[i].size;
    }
  }
  snapshot.total_bytes = manifest_.total_bytes();
  snapshot.checking = checking_;
  snapshot.current = current_;
  snapshot.error = error_;
  return snapshot;
}

}  // namespace onboarding
