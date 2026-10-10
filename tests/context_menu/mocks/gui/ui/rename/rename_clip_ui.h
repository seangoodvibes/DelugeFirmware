#pragma once
#include "context_menu_environment.h"
struct rename_clip_fixture : deluge::gui::ContextMenu {
	Clip* clip = nullptr;
	const char* getTitle() override { return "Rename"; }
	std::span<const char*> getOptions() override { return {}; }
};
inline session::State<rename_clip_fixture> rename_clips;
inline rename_clip_fixture& rename_clip_ui_for_session() {
	return rename_clips.active();
}
