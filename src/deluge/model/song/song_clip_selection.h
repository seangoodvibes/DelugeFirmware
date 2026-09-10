#pragma once

#include "gui/ui/ui_session.h"

class Clip;

// Non-owning navigation references. Musical clip data remains shared.
class SongClipSelection {
public:
	Clip* current() const { return panels_.active().current; }
	void select(Clip* clip) {
		auto& panel = panels_.active();
		if (panel.current)
			panel.previous = panel.current;
		panel.current = clip;
	}
	// Removal and replacement affect references in both panels, including history.
	void replace(Clip* oldClip, Clip* newClip) {
		if (!oldClip)
			return;
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			auto& panel = panels_.for_owner(owner);
			if (panel.current == oldClip)
				panel.current = newClip;
			if (panel.previous == oldClip)
				panel.previous = newClip;
		}
	}
	Clip* previous() const { return panels_.active().previous; }

private:
	struct Panel {
		Clip* current = nullptr;
		Clip* previous = nullptr;
	};
	deluge::gui::ui_session::State<Panel> panels_;
};
