#pragma once

#include "focus-monitor.hpp"

#include <QWidget>

#include <string>

extern "C" {
struct obs_data;
typedef struct obs_data obs_data_t;
}

class QLabel;
class QSpinBox;
class QTableWidget;
class QTimer;

class FocusDock : public QWidget {
	Q_OBJECT

public:
	explicit FocusDock(QWidget *parent = nullptr);
	~FocusDock();

	void save(obs_data_t *save_data, bool saving);

public slots:
	void rebuild();

private slots:
	void on_tick();
	void on_header_menu(const QPoint &pos);

private:
	void apply_columns();
	void update_status();
	QString status_text(const std::string &uuid, const FocusView &view) const;
	QString target_text(const FocusView &view) const;

	FocusMonitor monitor_;
	QTableWidget *table_ = nullptr;
	QLabel *empty_label_ = nullptr;
	QSpinBox *hide_delay_ = nullptr;
	QTimer *timer_ = nullptr;
	bool show_name_ = true;
	bool show_kind_ = true;
	bool show_target_ = true;
	bool show_status_ = true;
	std::string fingerprint_;
	bool force_rebuild_ = true;
	int list_refresh_ms_ = 0;
};
