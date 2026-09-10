#pragma once

#include "gui/ui/ui_session.h"

namespace deluge::model {
struct clip_length_resync_state {
	bool allowed = true;
};
inline gui::ui_session::State<clip_length_resync_state> clip_length_resync_states;
inline bool& clip_length_resync_allowed() {
	return clip_length_resync_states.active().allowed;
}
} // namespace deluge::model
