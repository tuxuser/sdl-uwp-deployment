#include "importer.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <winrt/windows.storage.fileproperties.h>
#include <winrt/windows.storage.streams.h>

namespace onboarding {
namespace {

using winrt::Windows::Storage::StorageFile;
using winrt::Windows::Storage::StorageFolder;
using winrt::Windows::Storage::Streams::Buffer;
using winrt::Windows::Storage::Streams::IInputStream;
using winrt::Windows::Storage::Streams::InputStreamOptions;

struct Candidate {
  std::string relative_path;
  StorageFile file;
};

void Collect(const StorageFolder& folder, const std::string& prefix, std::vector<Candidate>& out) {
  for (const auto& file : folder.GetFilesAsync().get()) {
    out.push_back({prefix + winrt::to_string(file.Name()), file});
  }
  for (const auto& child : folder.GetFoldersAsync().get()) {
    Collect(child, prefix + winrt::to_string(child.Name()) + "/", out);
  }
}

void ImportCandidates(FileStore& store, Catalog& catalog, const std::vector<Candidate>& candidates) {
  catalog.ClearError();
  const Manifest& manifest = catalog.manifest();
  // One source per manifest file; the first match wins.
  std::vector<std::pair<FileIndex, StorageFile>> work;
  std::vector<bool> taken(manifest.entries().size());
  for (const auto& candidate : candidates) {
    auto index = manifest.Match(candidate.relative_path);
    if (!index || taken[*index]) continue;
    taken[*index] = true;
    work.emplace_back(*index, candidate.file);
  }

  size_t failures = 0;
  std::string last_failure;
  for (const auto& [index, file] : work) {
    if (catalog.Take().files[index].status == FileStatus::kOk) continue;
    const ManifestEntry& entry = manifest.entries()[index];
    IInputStream stream{nullptr};
    try {
      // A wrong size is reported without touching the file's state, since
      // nothing would be written.
      if (file.GetBasicPropertiesAsync().get().Size() != entry.size) {
        ++failures;
        last_failure = entry.path + ": " + Describe(IngestResult::kSizeMismatch);
        continue;
      }
      stream = file.OpenSequentialReadAsync().get();
    } catch (const winrt::hresult_error&) {
      ++failures;
      last_failure = manifest.entries()[index].path + ": cannot open";
      continue;
    }
    Buffer buffer(1 << 20);
    auto read = [&](uint8_t* out, size_t capacity) -> std::optional<size_t> {
      try {
        auto chunk = stream.ReadAsync(buffer, static_cast<uint32_t>(std::min<size_t>(capacity, buffer.Capacity())),
                                      InputStreamOptions::None)
                         .get();
        std::memcpy(out, chunk.data(), chunk.Length());
        return chunk.Length();
      } catch (const winrt::hresult_error&) {
        return std::nullopt;
      }
    };
    IngestResult result = store.Ingest(index, read);
    if (result != IngestResult::kOk) {
      ++failures;
      last_failure = manifest.entries()[index].path + ": " + Describe(result);
      if (result == IngestResult::kDiskFull) break;
    }
  }

  if (work.empty()) {
    catalog.SetError("None of the picked files are in the manifest.");
  } else if (failures > 0 && catalog.Take().error.empty()) {
    catalog.SetError(std::to_string(failures) + " file(s) failed. " + last_failure);
  }
}

}  // namespace

void ImportFolder(FileStore& store, Catalog& catalog, const StorageFolder& folder) {
  std::vector<Candidate> candidates;
  try {
    Collect(folder, "", candidates);
  } catch (const winrt::hresult_error& error) {
    catalog.SetError("Cannot read the folder: " + winrt::to_string(error.message()));
    return;
  }
  ImportCandidates(store, catalog, candidates);
}

void ImportFiles(
    FileStore& store, Catalog& catalog,
    const winrt::Windows::Foundation::Collections::IVectorView<StorageFile>& files) {
  std::vector<Candidate> candidates;
  for (const auto& file : files) {
    // Only names are known, so matching relies on unique file names.
    candidates.push_back({winrt::to_string(file.Name()), file});
  }
  ImportCandidates(store, catalog, candidates);
}

}  // namespace onboarding
