// Copies manifest files out of a folder or file selection the user picked.
#pragma once

#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.storage.h>

#include "file_store.h"

namespace onboarding {

// Both run on a worker thread and block until done. Files that are already ok
// are skipped.
void ImportFolder(FileStore& store, Catalog& catalog,
                  const winrt::Windows::Storage::StorageFolder& folder);
void ImportFiles(
    FileStore& store, Catalog& catalog,
    const winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Storage::StorageFile>&
        files);

}  // namespace onboarding
