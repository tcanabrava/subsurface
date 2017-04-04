#ifndef SIMPLEWIDGETS_H
#define SIMPLEWIDGETS_H

class MinMaxAvgWidgetPrivate;
class QAbstractButton;
class QNetworkReply;

#include <QWidget>
#include <QGroupBox>
#include <QDialog>
#include <QTextEdit>
#include <stdint.h>

#include "ui_renumber.h"
#include "ui_setpoint.h"
#include "ui_shifttimes.h"
#include "ui_shiftimagetimes.h"
#include "ui_urldialog.h"
#include "ui_divecomponentselection.h"
#include "ui_listfilter.h"
#include "ui_filterwidget.h"
#include "core/exif.h"
#include "core/dive.h"


class MinMaxAvgWidget : public QWidget {
	Q_OBJECT
	Q_PROPERTY(double minimum READ minimum WRITE setMinimum)
	Q_PROPERTY(double maximum READ maximum WRITE setMaximum)
	Q_PROPERTY(double average READ average WRITE setAverage)
public:
	MinMaxAvgWidget(QWidget *parent);
	~MinMaxAvgWidget();
	double minimum() const;
	double maximum() const;
	double average() const;
	void setMinimum(double minimum);
	void setMaximum(double maximum);
	void setAverage(double average);
	void setMinimum(const QString &minimum);
	void setMaximum(const QString &maximum);
	void setAverage(const QString &average);
	void overrideMinToolTipText(const QString &newTip);
	void overrideAvgToolTipText(const QString &newTip);
	void overrideMaxToolTipText(const QString &newTip);
	void clear();

private:
	QScopedPointer<MinMaxAvgWidgetPrivate> d;
};

class RenumberDialog : public QDialog {
	Q_OBJECT
public:
	static RenumberDialog *instance();
	void renumberOnlySelected(bool selected = true);
private
slots:
	void buttonClicked(QAbstractButton *button);

private:
	explicit RenumberDialog(QWidget *parent);
	Ui::RenumberDialog ui;
	bool selectedOnly;
};

class SetpointDialog : public QDialog {
	Q_OBJECT
public:
	static SetpointDialog *instance();
	void setpointData(struct divecomputer *divecomputer, int time);
private
slots:
	void buttonClicked(QAbstractButton *button);

private:
	explicit SetpointDialog(QWidget *parent);
	Ui::SetpointDialog ui;
	struct divecomputer *dc;
	int time;
};

class ShiftTimesDialog : public QDialog {
	Q_OBJECT
public:
	static ShiftTimesDialog *instance();
	void showEvent(QShowEvent *event);
private
slots:
	void buttonClicked(QAbstractButton *button);
	void changeTime();

private:
	explicit ShiftTimesDialog(QWidget *parent);
	int64_t when;
	Ui::ShiftTimesDialog ui;
};

class ShiftImageTimesDialog : public QDialog {
	Q_OBJECT
public:
	explicit ShiftImageTimesDialog(QWidget *parent, QStringList fileNames);
	time_t amount() const;
	void setOffset(time_t offset);
	bool matchAll();
private
slots:
	void buttonClicked(QAbstractButton *button);
	void syncCameraClicked();
	void dcDateTimeChanged(const QDateTime &);
	void timeEditChanged(const QTime &time);
	void updateInvalid();
	void matchAllImagesToggled(bool);

private:
	QStringList fileNames;
	Ui::ShiftImageTimesDialog ui;
	time_t m_amount;
	time_t dcImageEpoch;
	bool matchAllImages;
};

class URLDialog : public QDialog {
	Q_OBJECT
public:
	explicit URLDialog(QWidget *parent);
	QString url() const;
private:
	Ui::URLDialog ui;
};

class QCalendarWidget;

class DiveComponentSelection : public QDialog {
	Q_OBJECT
public:
	explicit DiveComponentSelection(QWidget *parent, struct dive *target, struct dive_components *_what);
private
slots:
	void buttonClicked(QAbstractButton *button);

private:
	Ui::DiveComponentSelectionDialog ui;
	struct dive *targetDive;
	struct dive_components *what;
};

namespace Ui{
	class FilterWidget2;
};

class MultiFilter : public QWidget {
	Q_OBJECT
public
slots:
	void closeFilter();
	void adjustHeight();
	void filterFinished();

public:
	MultiFilter(QWidget *parent);
	Ui::FilterWidget2 ui;
};

class TagFilter : public QWidget {
	Q_OBJECT
public:
	TagFilter(QWidget *parent = 0);
	virtual void showEvent(QShowEvent *);
	virtual void hideEvent(QHideEvent *);

private:
	Ui::FilterWidget ui;
	friend class MultiFilter;
};

class BuddyFilter : public QWidget {
	Q_OBJECT
public:
	BuddyFilter(QWidget *parent = 0);
	virtual void showEvent(QShowEvent *);
	virtual void hideEvent(QHideEvent *);

private:
	Ui::FilterWidget ui;
};

class SuitFilter : public QWidget {
	Q_OBJECT
public:
	SuitFilter(QWidget *parent = 0);
	virtual void showEvent(QShowEvent *);
	virtual void hideEvent(QHideEvent *);

private:
	Ui::FilterWidget ui;
};

class LocationFilter : public QWidget {
	Q_OBJECT
public:
	LocationFilter(QWidget *parent = 0);
	virtual void showEvent(QShowEvent *);
	virtual void hideEvent(QHideEvent *);

private:
	Ui::FilterWidget ui;
};

class TextHyperlinkEventFilter : public QObject {
	Q_OBJECT
public:
	explicit TextHyperlinkEventFilter(QTextEdit *txtEdit);

	virtual bool eventFilter(QObject *target, QEvent *evt);

private:
	void handleUrlClick(const QString &urlStr);
	void handleUrlTooltip(const QString &urlStr, const QPoint &pos);
	bool stringMeetsOurUrlRequirements(const QString &maybeUrlStr);
	QString fromCursorTilWhitespace(QTextCursor *cursor, const bool searchBackwards);
	QString tryToFormulateUrl(QTextCursor *cursor);

	QTextEdit const *const textEdit;
	QWidget const *const scrollView;

	Q_DISABLE_COPY(TextHyperlinkEventFilter)
};

bool isGnome3Session();
QImage grayImage(const QImage &coloredImg);

#endif // SIMPLEWIDGETS_H
