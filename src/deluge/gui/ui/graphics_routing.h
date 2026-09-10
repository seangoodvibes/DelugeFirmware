#pragma once

#include "gui/ui/ui_session.h"

namespace deluge::gui::ui_session {

// Remote output is buffered by mirror transport, not the physical PIC UART.
// Keep the capacity query lazy so Remote never touches the hardware queue.
template <typename LocalReady>
bool graphics_output_ready(LocalReady local_ready) {
	return current() == Id::Remote || local_ready();
}

} // namespace deluge::gui::ui_session
