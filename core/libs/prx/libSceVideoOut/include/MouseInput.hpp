// SDL mouse-event router into the Mouse backend — input scope (M2-gated).
// Ported from AnyPS5 upstream/main. MouseInput::HandleEvent filters by
// window id and focus, then publishes via MousePublishInput_nid_postfix.
#ifndef CORE_LIBS_PRX_LIBSCEVIDEOOUT_MOUSEINPUT_HPP
#define CORE_LIBS_PRX_LIBSCEVIDEOOUT_MOUSEINPUT_HPP

#include "SDL_events.h"

class MouseInput {
public:
    void HandleEvent(const SDL_Event& event, unsigned windowId);

private:
    bool focused = true;
};

#endif
