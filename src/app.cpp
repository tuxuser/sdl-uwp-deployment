#include "app.h"

#include <cstdio>
#include <thread>

#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.devices.portable.h>
#include <winrt/windows.storage.pickers.h>
#include <winrt/windows.system.profile.h>

#include "net/lan_address.h"
#include "storage/importer.h"
#include "ui/draw.h"

namespace onboarding {
namespace {

using namespace ui;
using winrt::Windows::Storage::StorageFolder;
using winrt::Windows::Storage::Pickers::FileOpenPicker;
using winrt::Windows::Storage::Pickers::FolderPicker;
using winrt::Windows::Storage::Pickers::PickerLocationId;

constexpr size_t kMissingLines = 5;

std::string FormatBytes(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024 && unit < 4) {
    value /= 1024;
    ++unit;
  }
  char text[32];
  std::snprintf(text, sizeof(text), unit ? "%.1f %s" : "%.0f %s", value, units[unit]);
  return text;
}

// Keeps the end of a path, which is the informative part.
std::string Shorten(const std::string& text, size_t max_chars) {
  if (text.size() <= max_chars) return text;
  return "..." + text.substr(text.size() - (max_chars - 3));
}

// Runs `work` on a new thread that may block on WinRT async calls.
template <typename F>
void RunWorker(F work) {
  std::thread([work = std::move(work)]() mutable {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    work();
  }).detach();
}

}  // namespace

App::App() : catalog_(Manifest::Embedded()), store_(FileStore::DefaultRoot(), catalog_), server_(store_, catalog_) {}

void App::WatchUsbDrives() {
  using winrt::Windows::Devices::Enumeration::DeviceInformation;
  using winrt::Windows::Devices::Enumeration::DeviceInformationUpdate;
  using winrt::Windows::Devices::Enumeration::DeviceWatcher;
  is_xbox_ = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Xbox";
  if (!is_xbox_) return;
  // Events arrive on thread pool threads.
  usb_watcher_ = DeviceInformation::CreateWatcher(winrt::Windows::Devices::Portable::StorageDevice::GetDeviceSelector());
  usb_watcher_.Added([this](const DeviceWatcher&, const DeviceInformation& info) {
    std::lock_guard lock(usb_mutex_);
    usb_drives_.insert(std::wstring(info.Id()));
  });
  usb_watcher_.Removed([this](const DeviceWatcher&, const DeviceInformationUpdate& update) {
    std::lock_guard lock(usb_mutex_);
    usb_drives_.erase(std::wstring(update.Id()));
  });
  usb_watcher_.Start();
}

bool App::PickersAvailable() {
  if (!is_xbox_) return true;
  std::lock_guard lock(usb_mutex_);
  return !usb_drives_.empty();
}

void App::Start() {
  try {
    WatchUsbDrives();
  } catch (const winrt::hresult_error&) {
    // Without the watcher the pickers stay usable.
    is_xbox_ = false;
  }
  RunWorker([this] {
    store_.CheckAll();
    check_done_ = true;
  });
  RunWorker([this] {
    std::string error;
    bool ok = server_.Start(kHttpPort, &error);
    std::vector<std::string> urls;
    for (const auto& address : LanIPv4Addresses()) {
      urls.push_back("http://" + address + ":" + std::to_string(kHttpPort) + "/");
    }
    std::lock_guard lock(server_mutex_);
    urls_ = std::move(urls);
    server_error_ = ok ? "" : "Upload server failed to start: " + error;
  });
}

void App::HandleEvent(const SDL_Event& event) { buttons_.HandleEvent(event); }

void App::UpdateScreen(const Snapshot& snapshot) {
  switch (screen_) {
    case Screen::kChecking:
      if (check_done_) screen_ = snapshot.all_ready() ? Screen::kReady : Screen::kGetFiles;
      break;
    case Screen::kGetFiles:
      if (snapshot.all_ready() && !snapshot.busy() && !import_running_) screen_ = Screen::kReady;
      break;
    case Screen::kReady:
    case Screen::kDone:
      break;
  }
  if (screen_ == Screen::kGetFiles && buttons_for_ == screen_) {
    bool enabled = PickersAvailable() && !import_running_ && !picker_open_;
    buttons_.SetEnabled(0, enabled);
    buttons_.SetEnabled(1, enabled);
  }
  if (buttons_for_ == screen_) return;
  buttons_for_ = screen_;
  std::vector<Button> buttons;
  if (screen_ == Screen::kGetFiles) {
    buttons.push_back({"Pick a folder", {400, 300, 340, 60}, [this] { PickFolder(); }});
    buttons.push_back({"Pick files", {760, 300, 280, 60}, [this] { PickFiles(); }});
  } else if (screen_ == Screen::kReady) {
    buttons.push_back({"Continue", {kWidth / 2.0f - 150, 420, 300, 70}, [this] { screen_ = Screen::kDone; }});
  }
  buttons_.SetButtons(std::move(buttons));
  buttons_.Focus(0);
}

void App::Render(SDL_Renderer* renderer) {
  Snapshot snapshot = catalog_.Take();
  UpdateScreen(snapshot);

  SDL_SetRenderDrawColor(renderer, colors::kBackground.r, colors::kBackground.g, colors::kBackground.b, 255);
  SDL_RenderClear(renderer);
  switch (screen_) {
    case Screen::kChecking: RenderChecking(renderer, snapshot); break;
    case Screen::kGetFiles: RenderGetFiles(renderer, snapshot); break;
    case Screen::kReady: RenderReady(renderer); break;
    case Screen::kDone: RenderDone(renderer); break;
  }
  buttons_.Draw(renderer);
}

void App::RenderProgress(SDL_Renderer* renderer, const Snapshot& snapshot, float y) {
  uint64_t in_flight = 0;
  for (const auto& file : snapshot.files) {
    if (file.status == FileStatus::kTransferring || file.status == FileStatus::kVerifying) in_flight += file.done;
  }
  double overall;
  if (snapshot.checking) {
    // Files already decided count fully; the one being hashed counts so far.
    uint64_t checked = 0;
    for (const auto& file : snapshot.files) {
      if (file.status != FileStatus::kUnchecked && file.status != FileStatus::kVerifying) checked += file.entry->size;
    }
    overall = snapshot.total_bytes ? double(checked + in_flight) / snapshot.total_bytes : 1;
  } else {
    overall = snapshot.total_bytes ? double(snapshot.ready_bytes + in_flight) / snapshot.total_bytes : 1;
  }

  if (snapshot.current) {
    const FileView& file = snapshot.files[*snapshot.current];
    std::string label = std::string(file.status == FileStatus::kVerifying ? "Verifying " : "Receiving ") +
                        Shorten(file.entry->path, 50) + "  " + FormatBytes(file.done) + " / " +
                        FormatBytes(file.entry->size);
    Text(renderer, 60, y, 2, colors::kMuted, label);
    ProgressBar(renderer, {60, y + 24, kWidth - 120.0f, 14},
                file.entry->size ? double(file.done) / file.entry->size : 1);
  }
  Text(renderer, 60, y + 50, 2, colors::kMuted, "Overall");
  ProgressBar(renderer, {60, y + 74, kWidth - 120.0f, 14}, overall);
}

void App::RenderChecking(SDL_Renderer* renderer, const Snapshot& snapshot) {
  Text(renderer, 60, 60, 5, colors::kText, "Checking files");
  Text(renderer, 60, 130, 2, colors::kMuted,
       "Making sure the game files on this device are complete.");
  RenderProgress(renderer, snapshot, 300);
}

void App::RenderGetFiles(SDL_Renderer* renderer, const Snapshot& snapshot) {
  Text(renderer, 60, 40, 5, colors::kText, "Get your files in");

  std::vector<std::string> urls;
  std::string server_error;
  {
    std::lock_guard lock(server_mutex_);
    urls = urls_;
    server_error = server_error_;
  }
  if (!urls.empty()) {
    QrCode(renderer, 60, 100, 300, urls.front());
    Text(renderer, 60, 408, 2, colors::kMuted, "Scan with a phone");
  }
  Text(renderer, 400, 100, 2, colors::kMuted, "Open in a browser on the same network:");
  if (urls.empty()) {
    Text(renderer, 400, 130, 3, colors::kBad, server_error.empty() ? "No network connection" : "");
  }
  for (size_t i = 0; i < urls.size() && i < 3; ++i) {
    Text(renderer, 400, 130 + i * 34.0f, 3, colors::kText, urls[i]);
  }
  Text(renderer, 400, 270, 2, colors::kMuted,
       PickersAvailable() ? "Or copy from a folder or USB drive:"
                          : "Or insert a USB drive to copy the files from it.");

  Text(renderer, 400, 390, 3, colors::kText,
       std::to_string(snapshot.ready_count) + " of " + std::to_string(snapshot.files.size()) + " files ready");
  Text(renderer, 400, 422, 2, colors::kMuted,
       FormatBytes(snapshot.ready_bytes) + " of " + FormatBytes(snapshot.total_bytes));

  std::vector<const FileView*> missing;
  for (const auto& file : snapshot.files) {
    if (file.status != FileStatus::kOk) missing.push_back(&file);
  }
  float y = 452;
  if (!missing.empty()) {
    Text(renderer, 400, y, 2, colors::kMuted, "Missing or invalid:");
    for (size_t i = 0; i < missing.size() && i < kMissingLines; ++i) {
      y += 20;
      bool bad = missing[i]->status == FileStatus::kInvalid;
      Text(renderer, 400, y, 2, bad ? colors::kBad : colors::kText,
           Shorten(missing[i]->entry->path, 40) + (bad ? "  (invalid)" : ""));
    }
    if (missing.size() > kMissingLines) {
      y += 20;
      Text(renderer, 400, y, 2, colors::kMuted, "and " + std::to_string(missing.size() - kMissingLines) + " more");
    }
  }

  if (snapshot.busy() || import_running_) RenderProgress(renderer, snapshot, 588);

  std::string error = !snapshot.error.empty() ? snapshot.error : server_error;
  if (!error.empty()) Text(renderer, 60, 690, 2, colors::kBad, Shorten(error, 95));
}

void App::RenderReady(SDL_Renderer* renderer) {
  TextCentered(renderer, kWidth / 2.0f, 220, 5, colors::kText, "All files are ready");
  TextCentered(renderer, kWidth / 2.0f, 300, 2, colors::kMuted, "Every game file is on this device and verified.");
}

void App::RenderDone(SDL_Renderer* renderer) {
  TextCentered(renderer, kWidth / 2.0f, kHeight / 2.0f - 20, 6, colors::kAccent, "All good!");
}

winrt::fire_and_forget App::PickFolder() {
  if (picker_open_ || import_running_) co_return;
  picker_open_ = true;
  StorageFolder folder{nullptr};
  try {
    FolderPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    picker.FileTypeFilter().Append(L"*");
    folder = co_await picker.PickSingleFolderAsync();
  } catch (const winrt::hresult_error& error) {
    catalog_.SetError("Folder picker failed: " + winrt::to_string(error.message()));
  }
  picker_open_ = false;
  if (!folder) co_return;
  import_running_ = true;
  RunWorker([this, folder] {
    ImportFolder(store_, catalog_, folder);
    import_running_ = false;
  });
}

winrt::fire_and_forget App::PickFiles() {
  if (picker_open_ || import_running_) co_return;
  picker_open_ = true;
  winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Storage::StorageFile> files{nullptr};
  try {
    FileOpenPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    picker.FileTypeFilter().Append(L"*");
    files = co_await picker.PickMultipleFilesAsync();
  } catch (const winrt::hresult_error& error) {
    catalog_.SetError("File picker failed: " + winrt::to_string(error.message()));
  }
  picker_open_ = false;
  if (!files || files.Size() == 0) co_return;
  import_running_ = true;
  RunWorker([this, files] {
    ImportFiles(store_, catalog_, files);
    import_running_ = false;
  });
}

}  // namespace onboarding
