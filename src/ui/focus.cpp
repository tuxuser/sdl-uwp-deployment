#include "focus.h"

#include <cmath>
#include <limits>
#include <optional>

#include "draw.h"

namespace onboarding::ui {
namespace {

constexpr Sint16 kStickThreshold = 16000;

}  // namespace

void FocusGroup::SetButtons(std::vector<Button> buttons) {
  buttons_ = std::move(buttons);
  if (focus_ >= buttons_.size()) focus_ = 0;
}

std::optional<size_t> FocusGroup::HitTest(float x, float y) const {
  SDL_FPoint point{x, y};
  for (size_t i = 0; i < buttons_.size(); ++i) {
    if (SDL_PointInRectFloat(&point, &buttons_[i].rect)) return i;
  }
  return std::nullopt;
}

void FocusGroup::Activate(size_t index) {
  if (index < buttons_.size() && buttons_[index].enabled && buttons_[index].on_activate) {
    // Copy first: the callback may replace the buttons.
    auto action = buttons_[index].on_activate;
    action();
  }
}

void FocusGroup::SetEnabled(size_t index, bool enabled) {
  if (index >= buttons_.size() || buttons_[index].enabled == enabled) return;
  buttons_[index].enabled = enabled;
  // Keep the focus on something usable when its button turns off.
  if (!enabled && focus_ == index) {
    for (size_t i = 0; i < buttons_.size(); ++i) {
      if (buttons_[i].enabled) {
        focus_ = i;
        break;
      }
    }
  }
}

void FocusGroup::Move(int dx, int dy) {
  if (buttons_.empty()) return;
  // Pick the nearest button in the pressed direction.
  const SDL_FRect& from = buttons_[focus_].rect;
  float fx = from.x + from.w / 2, fy = from.y + from.h / 2;
  std::optional<size_t> best;
  float best_score = std::numeric_limits<float>::max();
  for (size_t i = 0; i < buttons_.size(); ++i) {
    if (i == focus_ || !buttons_[i].enabled) continue;
    const SDL_FRect& to = buttons_[i].rect;
    float tx = to.x + to.w / 2 - fx, ty = to.y + to.h / 2 - fy;
    float along = dx ? tx * dx : ty * dy;
    float across = dx ? std::fabs(ty) : std::fabs(tx);
    if (along <= 0) continue;
    float score = along + across * 2;
    if (score < best_score) {
      best_score = score;
      best = i;
    }
  }
  if (best) focus_ = *best;
}

bool FocusGroup::HandleEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: Move(0, -1); return true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: Move(0, 1); return true;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: Move(-1, 0); return true;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: Move(1, 0); return true;
        case SDL_GAMEPAD_BUTTON_SOUTH: Activate(focus_); return true;
        default: return false;
      }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      int* last = nullptr;
      if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) last = &stick_x_;
      if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) last = &stick_y_;
      if (!last) return false;
      int direction = event.gaxis.value > kStickThreshold ? 1 : event.gaxis.value < -kStickThreshold ? -1 : 0;
      if (direction != *last && direction != 0) {
        if (last == &stick_x_) Move(direction, 0); else Move(0, direction);
      }
      *last = direction;
      return true;
    }
    case SDL_EVENT_KEY_DOWN:
      switch (event.key.key) {
        case SDLK_UP: Move(0, -1); return true;
        case SDLK_DOWN: Move(0, 1); return true;
        case SDLK_LEFT: Move(-1, 0); return true;
        case SDLK_RIGHT: Move(1, 0); return true;
        case SDLK_TAB:
          if (!buttons_.empty()) {
            bool back = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            size_t next = focus_;
            for (size_t step = 0; step < buttons_.size(); ++step) {
              next = (next + (back ? buttons_.size() - 1 : 1)) % buttons_.size();
              if (buttons_[next].enabled) {
                focus_ = next;
                break;
              }
            }
          }
          return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
          Activate(focus_);
          return true;
        default:
          return false;
      }
    case SDL_EVENT_MOUSE_MOTION:
      if (auto hit = HitTest(event.motion.x, event.motion.y); hit && buttons_[*hit].enabled) focus_ = *hit;
      return false;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        if (auto hit = HitTest(event.button.x, event.button.y); hit && buttons_[*hit].enabled) {
          focus_ = *hit;
          Activate(*hit);
          return true;
        }
      }
      return false;
    default:
      return false;
  }
}

void FocusGroup::Draw(SDL_Renderer* renderer) const {
  for (size_t i = 0; i < buttons_.size(); ++i) {
    const Button& button = buttons_[i];
    bool focused = i == focus_ && button.enabled;
    FillRect(renderer, button.rect, focused ? Color{52, 58, 80} : colors::kPanel);
    if (focused) OutlineRect(renderer, button.rect, colors::kFocus, 4);
    Color text = button.enabled ? colors::kText : colors::kMuted;
    TextCentered(renderer, button.rect.x + button.rect.w / 2,
                 button.rect.y + (button.rect.h - kGlyph * 3) / 2, 3, text, button.label);
  }
}

}  // namespace onboarding::ui
