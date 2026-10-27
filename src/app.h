// Onboarding flow: check files, get missing files in, then continue.
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <winrt/windows.devices.enumeration.h>
#include <winrt/windows.foundation.h>

#include "net/http_server.h"
#include "storage/catalog.h"
#include "storage/file_store.h"
#include "ui/focus.h"

namespace onboarding {

constexpr uint16_t kHttpPort = 8080;

class App {
 public:
  App();

  // Starts the startup check and the HTTP server on worker threads.
  void Start();
  void HandleEvent(const SDL_Event& event);
  void Render(SDL_Renderer* renderer);

 private:
  enum class Screen { kChecking, kGetFiles, kReady, kDone };

  void UpdateScreen(const Snapshot& snapshot);
  void WatchUsbDrives();
  bool PickersAvailable();
  void RenderChecking(SDL_Renderer* renderer, const Snapshot& snapshot);
  void RenderGetFiles(SDL_Renderer* renderer, const Snapshot& snapshot);
  void RenderReady(SDL_Renderer* renderer);
  void RenderDone(SDL_Renderer* renderer);
  void RenderProgress(SDL_Renderer* renderer, const Snapshot& snapshot, float y);

  // Pickers must start on the UI thread; copying then runs on a worker.
  winrt::fire_and_forget PickFolder();
  winrt::fire_and_forget PickFiles();

  Catalog catalog_;
  FileStore store_;
  HttpServer server_;

  Screen screen_ = Screen::kChecking;
  ui::FocusGroup buttons_;
  // Set when the buttons for the current screen were built.
  std::optional<Screen> buttons_for_;
  std::atomic<bool> check_done_{false};
  std::atomic<bool> import_running_{false};
  bool picker_open_ = false;

  // On Xbox the pickers are only useful with a USB drive, so they are enabled
  // while one is connected. On a PC they are always enabled.
  bool is_xbox_ = false;
  winrt::Windows::Devices::Enumeration::DeviceWatcher usb_watcher_{nullptr};
  std::mutex usb_mutex_;
  std::set<std::wstring> usb_drives_;

  std::mutex server_mutex_;
  std::vector<std::string> urls_;
  std::string server_error_;
};

}  // namespace onboarding
