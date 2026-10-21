#include "focus-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct ListedSource {
	std::string uuid;
	std::string name;
	std::string kind_id;
	FocusView view;
};

struct SceneWalk {
	std::vector<ListedSource> *rows;
	std::unordered_set<std::string> seen_sources;
	std::unordered_set<std::string> seen_scenes;
};

bool is_capture(obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	return id && (std::strcmp(id, "window_capture") == 0 || std::strcmp(id, "game_capture") == 0);
}

void add_capture(SceneWalk *walk, obs_source_t *source)
{
	if (!source || !is_capture(source))
		return;
	const char *uuid = obs_source_get_uuid(source);
	if (!uuid || !walk->seen_sources.insert(uuid).second)
		return;

	ListedSource row;
	const char *name = obs_source_get_name(source);
	row.uuid = uuid;
	row.name = name ? name : "";
	row.kind_id = obs_source_get_id(source);
	walk->rows->push_back(std::move(row));
}

bool visit_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *param);
void walk_scene(SceneWalk *walk, obs_scene_t *scene);

bool visit_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *walk = static_cast<SceneWalk *>(param);
	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, visit_scene_item, param);
		return true;
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;
	if (obs_scene_t *nested = obs_scene_from_source(source))
		walk_scene(walk, nested);
	else
		add_capture(walk, source);
	return true;
}

void walk_scene(SceneWalk *walk, obs_scene_t *scene)
{
	if (!scene)
		return;
	obs_source_t *scene_source = obs_scene_get_source(scene);
	const char *uuid = scene_source ? obs_source_get_uuid(scene_source) : nullptr;
	if (uuid && !walk->seen_scenes.insert(uuid).second)
		return;
	obs_scene_enum_items(scene, visit_scene_item, walk);
}

std::vector<ListedSource> list_sources(FocusMonitor *monitor)
{
	std::vector<ListedSource> rows;
	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return rows;

	SceneWalk walk{&rows, {}, {}};
	if (obs_scene_t *scene = obs_scene_from_source(current))
		walk_scene(&walk, scene);
	obs_source_release(current);

	for (ListedSource &row : rows)
		if (obs_source_t *source = obs_get_source_by_uuid(row.uuid.c_str())) {
			row.view = monitor->inspect(source);
			obs_source_release(source);
		}
	return rows;
}

std::string fingerprint_of(const std::vector<ListedSource> &rows)
{
	std::string fingerprint;
	for (const ListedSource &row : rows) {
		fingerprint += row.uuid;
		fingerprint += '\n';
		fingerprint += row.name;
		fingerprint += '\n';
		fingerprint += row.kind_id;
		fingerprint += '\n';
		fingerprint += row.view.title;
		fingerprint += '\n';
		fingerprint += row.view.eligible ? '1' : '0';
		fingerprint += '\n';
		fingerprint += std::to_string(static_cast<int>(row.view.mode));
		fingerprint += '\n';
	}
	return fingerprint;
}

void on_frontend_event(enum obs_frontend_event event, void *private_data)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		QMetaObject::invokeMethod(static_cast<FocusDock *>(private_data), "rebuild", Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

} // namespace

FocusDock::FocusDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	auto *help = new QLabel(QString::fromUtf8(obs_module_text("Dock.Help")), this);
	help->setWordWrap(true);
	layout->addWidget(help);

	auto *delay_row = new QHBoxLayout();
	auto *delay_label = new QLabel(QString::fromUtf8(obs_module_text("HideDelay.Label")), this);
	hide_delay_ = new QSpinBox(this);
	hide_delay_->setRange(0, 3600);
	hide_delay_->setSuffix(QString::fromUtf8(obs_module_text("HideDelay.Suffix")));
	connect(hide_delay_, QOverload<int>::of(&QSpinBox::valueChanged), this,
		[this](int seconds) { monitor_.set_hide_delay_ms(seconds * 1000); });
	delay_row->addWidget(delay_label);
	delay_row->addWidget(hide_delay_);
	delay_row->addStretch(1);
	layout->addLayout(delay_row);

	table_ = new QTableWidget(this);
	table_->setColumnCount(6);
	table_->setHorizontalHeaderLabels({QString::fromUtf8(obs_module_text("Column.Enroll")),
					   QString::fromUtf8(obs_module_text("Column.Immediate")),
					   QString::fromUtf8(obs_module_text("Column.Name")),
					   QString::fromUtf8(obs_module_text("Column.Kind")),
					   QString::fromUtf8(obs_module_text("Column.Target")),
					   QString::fromUtf8(obs_module_text("Column.Status"))});
	table_->verticalHeader()->setVisible(false);
	table_->setSelectionMode(QAbstractItemView::NoSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setFocusPolicy(Qt::NoFocus);
	table_->horizontalHeader()->setStretchLastSection(false);
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(table_->horizontalHeader(), &QWidget::customContextMenuRequested, this, &FocusDock::on_header_menu);
	layout->addWidget(table_, 1);

	empty_label_ = new QLabel(QString::fromUtf8(obs_module_text("Empty.Label")), this);
	empty_label_->setWordWrap(true);
	layout->addWidget(empty_label_);

	auto *refresh = new QPushButton(QString::fromUtf8(obs_module_text("Button.Refresh")), this);
	connect(refresh, &QPushButton::clicked, this, &FocusDock::rebuild);
	layout->addWidget(refresh);

	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, &FocusDock::on_tick);
	timer_->start(100);

	obs_frontend_add_event_callback(on_frontend_event, this);
	rebuild();
}

FocusDock::~FocusDock()
{
	timer_->stop();
	obs_frontend_remove_event_callback(on_frontend_event, this);
}

void FocusDock::save(obs_data_t *save_data, bool saving)
{
	if (saving) {
		obs_data_t *obj = obs_data_create();
		obs_data_array_t *enrolled = obs_data_array_create();
		for (const std::string &uuid : monitor_.enrolled_uuids()) {
			obs_data_t *item = obs_data_create();
			obs_data_set_string(item, "uuid", uuid.c_str());
			obs_data_array_push_back(enrolled, item);
			obs_data_release(item);
		}
		obs_data_set_array(obj, "enrolled", enrolled);
		obs_data_array_release(enrolled);
		obs_data_array_t *immediate = obs_data_array_create();
		for (const std::string &uuid : monitor_.immediate_uuids()) {
			obs_data_t *item = obs_data_create();
			obs_data_set_string(item, "uuid", uuid.c_str());
			obs_data_array_push_back(immediate, item);
			obs_data_release(item);
		}
		obs_data_set_array(obj, "immediate", immediate);
		obs_data_array_release(immediate);
		obs_data_set_int(obj, "hide-delay-sec", hide_delay_->value());
		obs_data_set_bool(obj, "show-name", show_name_);
		obs_data_set_bool(obj, "show-kind", show_kind_);
		obs_data_set_bool(obj, "show-target", show_target_);
		obs_data_set_bool(obj, "show-status", show_status_);
		obs_data_set_obj(save_data, "focus-source", obj);
		obs_data_release(obj);
		return;
	}

	std::vector<std::string> uuids;
	obs_data_t *obj = obs_data_get_obj(save_data, "focus-source");
	obs_data_array_t *enrolled = obj ? obs_data_get_array(obj, "enrolled") : nullptr;
	const size_t count = enrolled ? obs_data_array_count(enrolled) : 0;
	uuids.reserve(count);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(enrolled, i);
		const char *uuid = obs_data_get_string(item, "uuid");
		if (uuid && uuid[0] != '\0')
			uuids.emplace_back(uuid);
		obs_data_release(item);
	}
	if (enrolled)
		obs_data_array_release(enrolled);
	std::vector<std::string> immediate_uuids;
	obs_data_array_t *immediate = obj ? obs_data_get_array(obj, "immediate") : nullptr;
	const size_t immediate_count = immediate ? obs_data_array_count(immediate) : 0;
	immediate_uuids.reserve(immediate_count);
	for (size_t i = 0; i < immediate_count; i++) {
		obs_data_t *item = obs_data_array_item(immediate, i);
		const char *uuid = obs_data_get_string(item, "uuid");
		if (uuid && uuid[0] != '\0')
			immediate_uuids.emplace_back(uuid);
		obs_data_release(item);
	}
	if (immediate)
		obs_data_array_release(immediate);
	if (obj) {
		obs_data_set_default_int(obj, "hide-delay-sec", 0);
		obs_data_set_default_bool(obj, "show-name", true);
		obs_data_set_default_bool(obj, "show-kind", true);
		obs_data_set_default_bool(obj, "show-target", true);
		obs_data_set_default_bool(obj, "show-status", true);
		hide_delay_->setValue(static_cast<int>(obs_data_get_int(obj, "hide-delay-sec")));
		show_name_ = obs_data_get_bool(obj, "show-name");
		show_kind_ = obs_data_get_bool(obj, "show-kind");
		show_target_ = obs_data_get_bool(obj, "show-target");
		show_status_ = obs_data_get_bool(obj, "show-status");
		obs_data_release(obj);
	}
	apply_columns();

	monitor_.replace_enrolled(uuids);
	monitor_.replace_immediate(immediate_uuids);
	force_rebuild_ = true;
	rebuild();
}

void FocusDock::apply_columns()
{
	table_->setColumnHidden(2, !show_name_);
	table_->setColumnHidden(3, !show_kind_);
	table_->setColumnHidden(4, !show_target_);
	table_->setColumnHidden(5, !show_status_);
}

void FocusDock::on_header_menu(const QPoint &pos)
{
	QMenu menu(this);
	auto add = [this, &menu](const char *key, bool checked, bool *flag, int column) {
		QAction *action = menu.addAction(QString::fromUtf8(obs_module_text(key)));
		action->setCheckable(true);
		action->setChecked(checked);
		connect(action, &QAction::toggled, this, [this, flag, column](bool on) {
			*flag = on;
			table_->setColumnHidden(column, !on);
		});
	};
	add("Column.Name", show_name_, &show_name_, 2);
	add("Column.Kind", show_kind_, &show_kind_, 3);
	add("Column.Target", show_target_, &show_target_, 4);
	add("Column.Status", show_status_, &show_status_, 5);
	menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void FocusDock::on_tick()
{
	monitor_.tick();
	list_refresh_ms_ += 100;
	if (force_rebuild_ || list_refresh_ms_ >= 1000) {
		list_refresh_ms_ = 0;
		rebuild();
		return;
	}
	update_status();
}

void FocusDock::rebuild()
{
	const std::vector<ListedSource> rows = list_sources(&monitor_);
	const std::string fingerprint = fingerprint_of(rows);
	if (!force_rebuild_ && fingerprint == fingerprint_) {
		update_status();
		return;
	}
	force_rebuild_ = false;
	fingerprint_ = fingerprint;

	const int scroll = table_->verticalScrollBar()->value();
	table_->setRowCount(static_cast<int>(rows.size()));
	for (int row_index = 0; row_index < static_cast<int>(rows.size()); row_index++) {
		const ListedSource &row = rows[static_cast<size_t>(row_index)];
		const bool enrolled = monitor_.is_enrolled(row.uuid);

		auto *wrap = new QWidget(table_);
		auto *box_layout = new QHBoxLayout(wrap);
		box_layout->setContentsMargins(0, 0, 0, 0);
		box_layout->setAlignment(Qt::AlignCenter);
		auto *box = new QCheckBox(wrap);
		box->setChecked(enrolled);
		box->setEnabled(row.view.eligible || enrolled);
		box_layout->addWidget(box);
		const std::string uuid = row.uuid;
		connect(box, &QCheckBox::toggled, this, [this, uuid](bool checked) {
			monitor_.set_enrolled(uuid, checked);
			update_status();
		});
		table_->setCellWidget(row_index, 0, wrap);

		auto *immediate_wrap = new QWidget(table_);
		auto *immediate_layout = new QHBoxLayout(immediate_wrap);
		immediate_layout->setContentsMargins(0, 0, 0, 0);
		immediate_layout->setAlignment(Qt::AlignCenter);
		auto *immediate = new QCheckBox(immediate_wrap);
		immediate->setChecked(monitor_.is_immediate(uuid));
		immediate_layout->addWidget(immediate);
		connect(immediate, &QCheckBox::toggled, this,
			[this, uuid](bool checked) { monitor_.set_immediate(uuid, checked); });
		table_->setCellWidget(row_index, 1, immediate_wrap);

		auto *name = new QTableWidgetItem(QString::fromUtf8(row.name.c_str()));
		name->setData(Qt::UserRole, QString::fromStdString(row.uuid));
		name->setData(Qt::UserRole + 1, row.view.eligible);
		name->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 2, name);

		const char *kind_key = row.kind_id == "game_capture" ? "Kind.GameCapture" : "Kind.WindowCapture";
		auto *kind = new QTableWidgetItem(QString::fromUtf8(obs_module_text(kind_key)));
		kind->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 3, kind);

		auto *target = new QTableWidgetItem(target_text(row.view));
		target->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 4, target);

		auto *status = new QTableWidgetItem(status_text(row.uuid, row.view));
		status->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 5, status);
	}

	table_->verticalScrollBar()->setValue(scroll);
	const bool empty = rows.empty();
	empty_label_->setVisible(empty);
	table_->setVisible(!empty);
}

void FocusDock::update_status()
{
	for (int row_index = 0; row_index < table_->rowCount(); row_index++) {
		QTableWidgetItem *name = table_->item(row_index, 2);
		QTableWidgetItem *status = table_->item(row_index, 5);
		if (!name || !status)
			continue;
		const std::string uuid = name->data(Qt::UserRole).toString().toStdString();
		if (!monitor_.is_enrolled(uuid)) {
			FocusView idle;
			idle.eligible = name->data(Qt::UserRole + 1).toBool();
			status->setText(status_text(uuid, idle));
			continue;
		}
		obs_source_t *source = obs_get_source_by_uuid(uuid.c_str());
		FocusView view;
		if (source) {
			view = monitor_.inspect(source);
			obs_source_release(source);
		}
		status->setText(status_text(uuid, view));
		if (QTableWidgetItem *target = table_->item(row_index, 4))
			target->setText(target_text(view));
	}
}

QString FocusDock::status_text(const std::string &uuid, const FocusView &view) const
{
	if (!view.eligible && !monitor_.is_enrolled(uuid))
		return QString::fromUtf8(obs_module_text("Status.Unsupported"));
	if (!monitor_.is_enrolled(uuid))
		return QString::fromUtf8(obs_module_text("Status.Idle"));
	if (!view.eligible)
		return QString::fromUtf8(obs_module_text("Status.Unsupported"));

	switch (view.state) {
	case FocusState::Showing:
		return QString::fromUtf8(obs_module_text("Status.Showing"));
	case FocusState::Hidden:
		return QString::fromUtf8(obs_module_text("Status.Hidden"));
	case FocusState::Missing:
		return QString::fromUtf8(obs_module_text("Status.Missing"));
	case FocusState::Unsupported:
		return QString::fromUtf8(obs_module_text("Status.Unsupported"));
	}

	return QString::fromUtf8(obs_module_text("Status.Unsupported"));
}

QString FocusDock::target_text(const FocusView &view) const
{
	if (!view.title.empty())
		return QString::fromUtf8(view.title.c_str());
	switch (view.mode) {
	case CaptureMode::Fullscreen:
		return QString::fromUtf8(obs_module_text("Target.Fullscreen"));
	case CaptureMode::Hotkey:
		return QString::fromUtf8(obs_module_text("Target.Hotkey"));
	case CaptureMode::Unset:
	case CaptureMode::Window:
		return QString::fromUtf8(obs_module_text("Target.Unset"));
	}

	return QString::fromUtf8(obs_module_text("Target.Unset"));
}
