#include "focus-monitor.hpp"

#include <util/bmem.h>

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

#ifdef _MSC_VER
#define FOCUS_IMPORT __declspec(dllimport)
#else
#define FOCUS_IMPORT
#endif

extern "C" {

enum window_priority {
	WINDOW_PRIORITY_CLASS,
	WINDOW_PRIORITY_TITLE,
	WINDOW_PRIORITY_EXE,
};

enum window_search_mode {
	INCLUDE_MINIMIZED,
	EXCLUDE_MINIMIZED,
};

FOCUS_IMPORT void ms_build_window_strings(const char *str, char **window_class, char **title, char **exe);
FOCUS_IMPORT HWND ms_find_window(enum window_search_mode mode, enum window_priority priority, const char *window_class,
				 const char *title, const char *exe);
}

struct WindowTarget {
	bool eligible = false;
	CaptureMode mode = CaptureMode::Unset;
	std::string title;
	int priority = WINDOW_PRIORITY_CLASS;
	char *window_class = nullptr;
	char *window_title = nullptr;
	char *executable = nullptr;
};

void free_target(WindowTarget *target)
{
	bfree(target->window_class);
	bfree(target->window_title);
	bfree(target->executable);
	target->window_class = nullptr;
	target->window_title = nullptr;
	target->executable = nullptr;
}

bool game_capture_tracks_window(obs_data_t *settings, CaptureMode *mode)
{
	const bool legacy = obs_data_has_user_value(settings, "capture_any_fullscreen") &&
			    !obs_data_has_user_value(settings, "capture_mode");
	if (legacy) {
		if (obs_data_get_bool(settings, "capture_any_fullscreen")) {
			*mode = CaptureMode::Fullscreen;
			return false;
		}
		*mode = CaptureMode::Window;
		return true;
	}

	const char *capture_mode = obs_data_get_string(settings, "capture_mode");
	if (capture_mode && std::strcmp(capture_mode, "window") == 0) {
		*mode = CaptureMode::Window;
		return true;
	}
	if (capture_mode && std::strcmp(capture_mode, "hotkey") == 0) {
		*mode = CaptureMode::Hotkey;
		return false;
	}
	*mode = CaptureMode::Fullscreen;
	return false;
}

WindowTarget read_target(obs_source_t *source)
{
	WindowTarget target;
	const char *id = obs_source_get_id(source);
	if (!id)
		return target;

	const bool window_capture = std::strcmp(id, "window_capture") == 0;
	const bool game_capture = std::strcmp(id, "game_capture") == 0;
	if (!window_capture && !game_capture)
		return target;

	obs_data_t *settings = obs_source_get_settings(source);
	if (game_capture && !game_capture_tracks_window(settings, &target.mode)) {
		obs_data_release(settings);
		return target;
	}

	target.eligible = true;
	target.mode = CaptureMode::Window;
	target.priority = static_cast<int>(obs_data_get_int(settings, "priority"));
	const char *window = obs_data_get_string(settings, "window");
	if (window && window[0] != '\0') {
		ms_build_window_strings(window, &target.window_class, &target.window_title, &target.executable);
		if (target.window_title)
			target.title = target.window_title;
	} else {
		target.mode = CaptureMode::Unset;
	}
	obs_data_release(settings);
	return target;
}

bool same_top_level(HWND foreground, HWND captured)
{
	if (!foreground || !captured)
		return false;
	if (foreground == captured)
		return true;

	HWND foreground_root = GetAncestor(foreground, GA_ROOT);
	HWND captured_root = GetAncestor(captured, GA_ROOT);
	if (!foreground_root)
		foreground_root = foreground;
	if (!captured_root)
		captured_root = captured;
	return foreground_root == captured_root;
}

FocusState focus_state(const WindowTarget &target)
{
	if (!target.eligible)
		return FocusState::Unsupported;
	if (!target.window_class)
		return FocusState::Missing;

	HWND captured = ms_find_window(INCLUDE_MINIMIZED, static_cast<window_priority>(target.priority),
				       target.window_class, target.window_title, target.executable);
	if (!captured)
		return FocusState::Missing;
	if (same_top_level(GetForegroundWindow(), captured))
		return FocusState::Showing;
	return FocusState::Hidden;
}

template<typename Fn> struct ItemPayload {
	const std::string *uuid;
	Fn *fn;
};

template<typename Fn> bool visit_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *payload = static_cast<ItemPayload<Fn> *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, visit_item<Fn>, param);

	obs_source_t *source = obs_sceneitem_get_source(item);
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	if (!uuid || *payload->uuid != uuid)
		return true;

	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *scene_source = scene ? obs_scene_get_source(scene) : nullptr;
	const char *scene_uuid = scene_source ? obs_source_get_uuid(scene_source) : nullptr;
	(*payload->fn)(item, std::string(scene_uuid ? scene_uuid : ""));
	return true;
}

template<typename Fn> bool visit_scene(void *param, obs_source_t *scene_source)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene)
		obs_scene_enum_items(scene, visit_item<Fn>, param);
	return true;
}

template<typename Fn> void for_each_item(const std::string &uuid, Fn fn)
{
	ItemPayload<Fn> payload{&uuid, &fn};
	obs_enum_scenes(visit_scene<Fn>, &payload);
}

} // namespace

void FocusMonitor::set_enrolled(const std::string &uuid, bool enrolled)
{
	const auto it = std::find(enrolled_.begin(), enrolled_.end(), uuid);
	if (enrolled) {
		if (it == enrolled_.end()) {
			enrolled_.push_back(uuid);
			remember(uuid);
		}
		apply(uuid);
		return;
	}

	if (it == enrolled_.end())
		return;
	restore(uuid);
	enrolled_.erase(it);
	lost_since_.erase(uuid);
}

void FocusMonitor::replace_enrolled(const std::vector<std::string> &uuids)
{
	enrolled_ = uuids;
	snapshots_.clear();
	lost_since_.clear();
}

void FocusMonitor::set_hide_delay_ms(int ms)
{
	hide_delay_ms_ = ms < 0 ? 0 : ms;
}

int FocusMonitor::hide_delay_ms() const
{
	return hide_delay_ms_;
}

void FocusMonitor::set_immediate(const std::string &uuid, bool immediate)
{
	if (immediate)
		immediate_.insert(uuid);
	else
		immediate_.erase(uuid);
	if (is_enrolled(uuid))
		apply(uuid);
}

void FocusMonitor::replace_immediate(const std::vector<std::string> &uuids)
{
	immediate_.clear();
	immediate_.insert(uuids.begin(), uuids.end());
}

bool FocusMonitor::is_immediate(const std::string &uuid) const
{
	return immediate_.find(uuid) != immediate_.end();
}

std::vector<std::string> FocusMonitor::immediate_uuids() const
{
	return {immediate_.begin(), immediate_.end()};
}

bool FocusMonitor::is_enrolled(const std::string &uuid) const
{
	return std::find(enrolled_.begin(), enrolled_.end(), uuid) != enrolled_.end();
}

std::vector<std::string> FocusMonitor::enrolled_uuids() const
{
	return enrolled_;
}

void FocusMonitor::tick()
{
	for (const std::string &uuid : enrolled_)
		apply(uuid);
}

FocusView FocusMonitor::inspect(obs_source_t *source) const
{
	FocusView view;
	if (!source)
		return view;

	WindowTarget target = read_target(source);
	view.eligible = target.eligible;
	view.mode = target.mode;
	view.title = target.title;
	view.state = focus_state(target);
	free_target(&target);
	return view;
}

void FocusMonitor::remember(const std::string &uuid)
{
	snapshots_.erase(std::remove_if(snapshots_.begin(), snapshots_.end(),
					[&uuid](const Snapshot &snapshot) { return snapshot.source_uuid == uuid; }),
			 snapshots_.end());

	for_each_item(uuid, [this, &uuid](obs_sceneitem_t *item, const std::string &scene_uuid) {
		Snapshot snapshot;
		snapshot.source_uuid = uuid;
		snapshot.scene_uuid = scene_uuid;
		snapshot.item_id = obs_sceneitem_get_id(item);
		snapshot.visible = obs_sceneitem_visible(item);
		snapshots_.push_back(std::move(snapshot));
	});
}

void FocusMonitor::restore(const std::string &uuid)
{
	for_each_item(uuid, [this, &uuid](obs_sceneitem_t *item, const std::string &scene_uuid) {
		const int64_t item_id = obs_sceneitem_get_id(item);
		bool visible = true;
		for (const Snapshot &snapshot : snapshots_) {
			if (snapshot.source_uuid == uuid && snapshot.scene_uuid == scene_uuid &&
			    snapshot.item_id == item_id) {
				visible = snapshot.visible;
				break;
			}
		}
		if (obs_sceneitem_visible(item) != visible)
			obs_sceneitem_set_visible(item, visible);
	});

	snapshots_.erase(std::remove_if(snapshots_.begin(), snapshots_.end(),
					[&uuid](const Snapshot &snapshot) { return snapshot.source_uuid == uuid; }),
			 snapshots_.end());
}

void FocusMonitor::apply(const std::string &uuid)
{
	obs_source_t *source = obs_get_source_by_uuid(uuid.c_str());
	if (!source)
		return;

	const FocusView view = inspect(source);
	obs_source_release(source);

	const bool focused = view.state == FocusState::Showing;
	const int delay_ms = immediate_.find(uuid) != immediate_.end() ? 0 : hide_delay_ms_;
	bool show = focused;
	if (focused) {
		lost_since_.erase(uuid);
	} else {
		const auto now = std::chrono::steady_clock::now();
		const auto [it, inserted] = lost_since_.emplace(uuid, now);
		if (inserted && delay_ms == 0)
			show = false;
		else {
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
			show = elapsed.count() < delay_ms;
		}
	}
	for_each_item(uuid, [show](obs_sceneitem_t *item, const std::string &) {
		if (obs_sceneitem_visible(item) != show)
			obs_sceneitem_set_visible(item, show);
	});
}
