#pragma once

#include <obs.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class FocusState {
	Unsupported,
	Missing,
	Showing,
	Hidden,
};

enum class CaptureMode {
	Window,
	Fullscreen,
	Hotkey,
	Unset,
};

struct FocusView {
	bool eligible = false;
	CaptureMode mode = CaptureMode::Unset;
	std::string title;
	FocusState state = FocusState::Unsupported;
};

class FocusMonitor {
public:
	void set_enrolled(const std::string &uuid, bool enrolled);
	void replace_enrolled(const std::vector<std::string> &uuids);
	bool is_enrolled(const std::string &uuid) const;
	std::vector<std::string> enrolled_uuids() const;

	void set_hide_delay_ms(int ms);
	int hide_delay_ms() const;

	void set_immediate(const std::string &uuid, bool immediate);
	void replace_immediate(const std::vector<std::string> &uuids);
	bool is_immediate(const std::string &uuid) const;
	std::vector<std::string> immediate_uuids() const;

	void tick();
	FocusView inspect(obs_source_t *source) const;

private:
	struct Snapshot {
		std::string source_uuid;
		std::string scene_uuid;
		int64_t item_id;
		bool visible;
	};

	void remember(const std::string &uuid);
	void restore(const std::string &uuid);
	void apply(const std::string &uuid);

	std::vector<std::string> enrolled_;
	std::unordered_set<std::string> immediate_;
	std::vector<Snapshot> snapshots_;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> lost_since_;
	int hide_delay_ms_ = 0;
};
