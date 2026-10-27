// Buttons with one focused at a time. Controller (D-pad, left stick, A),
// keyboard (arrows, Tab, Enter, Space) and mouse all drive the same focus.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace onboarding::ui {

struct Button {
  std::string label;
  SDL_FRect rect;
  std::function<void()> on_activate;
  bool enabled = true;
};

class FocusGroup {
 public:
  // Replaces the buttons; focus is kept by index when possible.
  void SetButtons(std::vector<Button> buttons);
  // Returns true when the event was used.
  bool HandleEvent(const SDL_Event& event);
  void Draw(SDL_Renderer* renderer) const;

  void Focus(size_t index) { focus_ = index; }
  void SetEnabled(size_t index, bool enabled);

 private:
  void Move(int dx, int dy);
  void Activate(size_t index);
  std::optional<size_t> HitTest(float x, float y) const;

  std::vector<Button> buttons_;
  size_t focus_ = 0;
  // Last left-stick direction, so a held stick moves focus once.
  int stick_x_ = 0;
  int stick_y_ = 0;
};

}  // namespace onboarding::ui
