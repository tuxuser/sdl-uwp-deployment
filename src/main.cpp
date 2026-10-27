// SDL3-uwp's SDL_main.h provides the WinRT entry point. It runs this main on
// the CoreWindow thread through CoreApplication::Run.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.h"
#include "ui/draw.h"

int main(int, char**) {
  // SDL drops gamepad input while no SDL window holds keyboard focus. The
  // CoreWindow is the only window and is always in front.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  // The CoreWindow must be activated soon after launch, or the system ends
  // the app. Activation is handled when events are pumped.
  SDL_PumpEvents();

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("Onboarding", onboarding::ui::kWidth, onboarding::ui::kHeight,
                                   SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
    return 1;
  }
  SDL_SetRenderLogicalPresentation(renderer, onboarding::ui::kWidth, onboarding::ui::kHeight,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderVSync(renderer, 1);

  // Worker threads refer to the app until the process ends, so it is never
  // destroyed.
  auto* app = new onboarding::App();
  app->Start();

  for (;;) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) return 0;
      if (event.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(event.gdevice.which);
      SDL_ConvertEventToRenderCoordinates(renderer, &event);
      app->HandleEvent(event);
    }
    app->Render(renderer);
    SDL_RenderPresent(renderer);
  }
}
