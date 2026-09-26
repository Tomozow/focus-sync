#include "focus-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>
#include <utility>
#include <vector>

namespace {

struct ListedSource {
	std::string uuid;
	std::string name;
	std::string kind_id;
	FocusView view;
};

struct ListCollect {
	std::vector<ListedSource> *rows;
};

bool collect_source(void *param, obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	if (!id)
		return true;
	if (std::strcmp(id, "window_capture") != 0 && std::strcmp(id, "game_capture") != 0)
		return true;

	auto *collect = static_cast<ListCollect *>(param);
	ListedSource row;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	row.uuid = uuid ? uuid : "";
	row.name = name ? name : "";
	row.kind_id = id;
	collect->rows->push_back(std::move(row));
	return true;
}

std::vector<ListedSource> list_sources(FocusMonitor *monitor)
{
	std::vector<ListedSource> rows;
	ListCollect collect{&rows};
	obs_enum_sources(collect_source, &collect);
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

	table_ = new QTableWidget(this);
	table_->setColumnCount(5);
	table_->setHorizontalHeaderLabels(
		{QString::fromUtf8(obs_module_text("Column.Enroll")), QString::fromUtf8(obs_module_text("Column.Name")),
		 QString::fromUtf8(obs_module_text("Column.Kind")), QString::fromUtf8(obs_module_text("Column.Target")),
		 QString::fromUtf8(obs_module_text("Column.Status"))});
	table_->verticalHeader()->setVisible(false);
	table_->setSelectionMode(QAbstractItemView::NoSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setFocusPolicy(Qt::NoFocus);
	table_->horizontalHeader()->setStretchLastSection(false);
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
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
	if (obj)
		obs_data_release(obj);

	monitor_.replace_enrolled(uuids);
	force_rebuild_ = true;
	rebuild();
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

		auto *name = new QTableWidgetItem(QString::fromUtf8(row.name.c_str()));
		name->setData(Qt::UserRole, QString::fromStdString(row.uuid));
		name->setData(Qt::UserRole + 1, row.view.eligible);
		name->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 1, name);

		const char *kind_key = row.kind_id == "game_capture" ? "Kind.GameCapture" : "Kind.WindowCapture";
		auto *kind = new QTableWidgetItem(QString::fromUtf8(obs_module_text(kind_key)));
		kind->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 2, kind);

		auto *target = new QTableWidgetItem(target_text(row.view));
		target->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 3, target);

		auto *status = new QTableWidgetItem(status_text(row.uuid, row.view));
		status->setFlags(Qt::ItemIsEnabled);
		table_->setItem(row_index, 4, status);
	}

	table_->verticalScrollBar()->setValue(scroll);
	const bool empty = rows.empty();
	empty_label_->setVisible(empty);
	table_->setVisible(!empty);
}

void FocusDock::update_status()
{
	for (int row_index = 0; row_index < table_->rowCount(); row_index++) {
		QTableWidgetItem *name = table_->item(row_index, 1);
		QTableWidgetItem *status = table_->item(row_index, 4);
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
		if (QTableWidgetItem *target = table_->item(row_index, 3))
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
