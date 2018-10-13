// SPDX-License-Identifier: GPL-2.0
#include "desktop-widgets/locationinformation.h"
#include "core/subsurface-string.h"
#include "desktop-widgets/mainwindow.h"
#include "desktop-widgets/divelistview.h"
#include "core/qthelper.h"
#include "desktop-widgets/mapwidget.h"
#include "qt-models/filtermodels.h"
#include "core/divesitehelpers.h"
#include "desktop-widgets/modeldelegates.h"

#include <QDebug>
#include <QShowEvent>
#include <QItemSelectionModel>
#include <qmessagebox.h>
#include <cstdlib>
#include <QDesktopWidget>
#include <QScrollBar>

LocationInformationWidget::LocationInformationWidget(QWidget *parent) : QGroupBox(parent), modified(false), diveSite(nullptr)
{
	memset(&taxonomy, 0, sizeof(taxonomy));
	ui.setupUi(this);
	ui.diveSiteMessage->setCloseButtonVisible(false);

	acceptAction = new QAction(tr("Apply changes"), this);
	connect(acceptAction, SIGNAL(triggered(bool)), this, SLOT(acceptChanges()));

	rejectAction = new QAction(tr("Discard changes"), this);
	connect(rejectAction, SIGNAL(triggered(bool)), this, SLOT(rejectChanges()));

	ui.diveSiteMessage->setText(tr("Dive site management"));
	ui.diveSiteMessage->addAction(acceptAction);
	ui.diveSiteMessage->addAction(rejectAction);

	connect(this, SIGNAL(startFilterDiveSite(uint32_t)), MultiFilterSortModel::instance(), SLOT(startFilterDiveSite(uint32_t)));
	connect(this, SIGNAL(stopFilterDiveSite()), MultiFilterSortModel::instance(), SLOT(stopFilterDiveSite()));
	connect(ui.geoCodeButton, SIGNAL(clicked()), this, SLOT(reverseGeocode()));
	connect(this, SIGNAL(nameChanged(const QString &, const QString &)),
		LocationFilterModel::instance(), SLOT(changeName(const QString &, const QString &)));
	connect(ui.updateLocationButton, SIGNAL(clicked()), this, SLOT(updateLocationOnMap()));
	connect(ui.diveSiteCoordinates, SIGNAL(returnPressed()), this, SLOT(updateLocationOnMap()));
	ui.diveSiteCoordinates->installEventFilter(this);

	ui.diveSiteListView->setModel(&filter_model);
	ui.diveSiteListView->setModelColumn(LocationInformationModel::NAME);
	ui.diveSiteListView->installEventFilter(this);
	// Map Management Code.
	connect(MapWidget::instance(), &MapWidget::coordinatesChanged,
		this, &LocationInformationWidget::updateGpsCoordinates);
}

bool LocationInformationWidget::eventFilter(QObject *object, QEvent *ev)
{
	if (ev->type() == QEvent::ContextMenu) {
		QContextMenuEvent *ctx = (QContextMenuEvent *)ev;
		QMenu contextMenu;
		contextMenu.addAction(tr("Merge into current site"), this, SLOT(mergeSelectedDiveSites()));
		contextMenu.exec(ctx->globalPos());
		return true;
	} else if (ev->type() == QEvent::FocusOut && object == ui.diveSiteCoordinates) {
		updateLocationOnMap();
	}
	return false;
}

void LocationInformationWidget::enableLocationButtons(bool enable)
{
	ui.geoCodeButton->setEnabled(enable);
	ui.updateLocationButton->setEnabled(enable);
}

void LocationInformationWidget::mergeSelectedDiveSites()
{
	if (!diveSite)
		return;
	if (QMessageBox::warning(MainWindow::instance(), tr("Merging dive sites"),
				 tr("You are about to merge dive sites, you can't undo that action \n Are you sure you want to continue?"),
				 QMessageBox::Ok, QMessageBox::Cancel) != QMessageBox::Ok)
		return;

	QModelIndexList selection = ui.diveSiteListView->selectionModel()->selectedIndexes();
	uint32_t *selected_dive_sites = (uint32_t *)malloc(sizeof(uint32_t) * selection.count());
	int i = 0;
	Q_FOREACH (const QModelIndex &idx, selection) {
		selected_dive_sites[i] = (uint32_t)idx.data(LocationInformationModel::UUID_ROLE).toInt();
		i++;
	}
	merge_dive_sites(diveSite->uuid, selected_dive_sites, i);
	LocationInformationModel::instance()->update();
	QSortFilterProxyModel *m = (QSortFilterProxyModel *)ui.diveSiteListView->model();
	m->invalidate();
	free(selected_dive_sites);
}

void LocationInformationWidget::updateLabels()
{
	if (!diveSite) {
		clearLabels();
		return;
	}
	if (diveSite->name)
		ui.diveSiteName->setText(diveSite->name);
	else
		ui.diveSiteName->clear();
	const char *country = taxonomy_get_country(&taxonomy);
	if (country)
		ui.diveSiteCountry->setText(country);
	else
		ui.diveSiteCountry->clear();
	if (diveSite->description)
		ui.diveSiteDescription->setText(diveSite->description);
	else
		ui.diveSiteDescription->clear();
	if (diveSite->notes)
		ui.diveSiteNotes->setPlainText(diveSite->notes);
	else
		ui.diveSiteNotes->clear();
	if (diveSite->latitude.udeg || diveSite->longitude.udeg) {
		const char *coords = printGPSCoords(diveSite->latitude.udeg, diveSite->longitude.udeg);
		ui.diveSiteCoordinates->setText(coords);
		free((void *)coords);
	} else {
		ui.diveSiteCoordinates->clear();
	}

	ui.locationTags->setText(constructLocationTags(&taxonomy, false));
}

void LocationInformationWidget::clearLabels()
{
	ui.diveSiteName->clear();
	ui.diveSiteCountry->clear();
	ui.diveSiteDescription->clear();
	ui.diveSiteNotes->clear();
	ui.diveSiteCoordinates->clear();
	ui.locationTags->clear();
}

void LocationInformationWidget::updateGpsCoordinates(degrees_t latitude, degrees_t longitude)
{
	QString oldText = ui.diveSiteCoordinates->text();
	const char *coords = printGPSCoords(latitude.udeg, longitude.udeg);
	ui.diveSiteCoordinates->setText(coords);
	enableLocationButtons(latitude.udeg || longitude.udeg);
	free((void *)coords);
	if (oldText != ui.diveSiteCoordinates->text())
		markChangedWidget(ui.diveSiteCoordinates);
}

// Parse GPS text into latitude and longitude.
// On error, false is returned and the output parameters are left unmodified.
bool parseGpsText(const QString &text, degrees_t &latitude, degrees_t &longitude)
{
	double lat, lon;
	if (parseGpsText(text, &lat, &lon)) {
		latitude.udeg = lrint(lat * 1000000.0);
		longitude.udeg = lrint(lon * 1000000.0);
		return true;
	}
	return false;
}

void LocationInformationWidget::acceptChanges()
{
	if (!diveSite) {
		qWarning() << "did not have valid dive site in LocationInformationWidget";
		return;
	}

	char *uiString;
	uiString = copy_qstring(ui.diveSiteName->text());
	if (!same_string(uiString, diveSite->name)) {
		emit nameChanged(QString(diveSite->name), ui.diveSiteName->text());
		free(diveSite->name);
		diveSite->name = uiString;
	} else {
		free(uiString);
	}
	uiString = copy_qstring(ui.diveSiteDescription->text());
	if (!same_string(uiString, diveSite->description)) {
		free(diveSite->description);
		diveSite->description = uiString;
	} else {
		free(uiString);
	}
	uiString = copy_qstring(ui.diveSiteCountry->text());
	// if the user entered a different country, first update the local taxonomy
	// this below will get copied into the diveSite
	if (!same_string(uiString, taxonomy_get_country(&taxonomy)) &&
	    !empty_string(uiString))
		taxonomy_set_country(&taxonomy, uiString, taxonomy_origin::GEOMANUAL);
	else
		free(uiString);
	// now update the diveSite
	copy_taxonomy(&taxonomy, &diveSite->taxonomy);

	uiString = copy_qstring(ui.diveSiteNotes->document()->toPlainText());
	if (!same_string(uiString, diveSite->notes)) {
		free(diveSite->notes);
		diveSite->notes = uiString;
	} else {
		free(uiString);
	}

	if (!ui.diveSiteCoordinates->text().isEmpty())
		parseGpsText(ui.diveSiteCoordinates->text(), diveSite->latitude, diveSite->longitude);
	if (dive_site_is_empty(diveSite)) {
		LocationInformationModel::instance()->removeRow(get_divesite_idx(diveSite));
		displayed_dive.dive_site_uuid = 0;
		diveSite = nullptr;
	}
	mark_divelist_changed(true);
	resetState();
}

void LocationInformationWidget::rejectChanges()
{
	resetState();
}

void LocationInformationWidget::initFields(dive_site *ds)
{
	diveSite = ds;
	if (ds) {
		copy_taxonomy(&ds->taxonomy, &taxonomy);
		filter_model.set(ds->uuid, ds->latitude, ds->longitude);
		updateLabels();
		enableLocationButtons(dive_site_has_gps_location(ds));
		QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(ui.diveSiteListView->model());
		emit startFilterDiveSite(ds->uuid);
		if (m)
			m->invalidate();
	} else {
		free_taxonomy(&taxonomy);
		filter_model.set(0, degrees_t{ 0 }, degrees_t{ 0 });
		clearLabels();
	}
	MapWidget::instance()->prepareForGetDiveCoordinates(ds ? ds->uuid : 0);
}

void LocationInformationWidget::markChangedWidget(QWidget *w)
{
	QPalette p;
	qreal h, s, l, a;
	if (!modified)
		enableEdition();
	qApp->palette().color(QPalette::Text).getHslF(&h, &s, &l, &a);
	p.setBrush(QPalette::Base, (l <= 0.3) ? QColor(Qt::yellow).lighter() : (l <= 0.6) ? QColor(Qt::yellow).light() : /* else */ QColor(Qt::yellow).darker(300));
	w->setPalette(p);
	modified = true;
}

void LocationInformationWidget::resetState()
{
	modified = false;
	resetPallete();
	MainWindow::instance()->dive_list()->setEnabled(true);
	MainWindow::instance()->setEnabledToolbar(true);
	ui.diveSiteMessage->setText(tr("Dive site management"));
	MapWidget::instance()->endGetDiveCoordinates();
	MapWidget::instance()->repopulateLabels();
	emit stopFilterDiveSite();
	emit endEditDiveSite();
	updateLocationOnMap();
}

void LocationInformationWidget::enableEdition()
{
	MainWindow::instance()->dive_list()->setEnabled(false);
	MainWindow::instance()->setEnabledToolbar(false);
	ui.diveSiteMessage->setText(tr("You are editing a dive site"));
}

void LocationInformationWidget::on_diveSiteCoordinates_textChanged(const QString &text)
{
	if (!diveSite)
		return;
	degrees_t latitude, longitude;
	bool ok_old = diveSite->latitude.udeg || diveSite->longitude.udeg;
	bool ok = parseGpsText(text, latitude, longitude);
	if (ok != ok_old || latitude.udeg != diveSite->latitude.udeg || longitude.udeg != diveSite->longitude.udeg) {
		if (ok) {
			markChangedWidget(ui.diveSiteCoordinates);
			enableLocationButtons(true);
			filter_model.setCoordinates(latitude, longitude);
		} else {
			enableLocationButtons(false);
		}
	}
}

void LocationInformationWidget::on_diveSiteCountry_textChanged(const QString& text)
{
	if (!same_string(qPrintable(text), taxonomy_get_country(&taxonomy)))
		markChangedWidget(ui.diveSiteCountry);
}

void LocationInformationWidget::on_diveSiteDescription_textChanged(const QString &text)
{
	if (diveSite && !same_string(qPrintable(text), diveSite->description))
		markChangedWidget(ui.diveSiteDescription);
}

void LocationInformationWidget::on_diveSiteName_textChanged(const QString &text)
{
	if (diveSite && !same_string(qPrintable(text), diveSite->name))
		markChangedWidget(ui.diveSiteName);
}

void LocationInformationWidget::on_diveSiteNotes_textChanged()
{
	if (diveSite && !same_string(qPrintable(ui.diveSiteNotes->toPlainText()), diveSite->notes))
		markChangedWidget(ui.diveSiteNotes);
}

void LocationInformationWidget::resetPallete()
{
	QPalette p;
	ui.diveSiteCoordinates->setPalette(p);
	ui.diveSiteDescription->setPalette(p);
	ui.diveSiteCountry->setPalette(p);
	ui.diveSiteName->setPalette(p);
	ui.diveSiteNotes->setPalette(p);
}

void LocationInformationWidget::reverseGeocode()
{
	degrees_t latitude, longitude;
	if (!parseGpsText(ui.diveSiteCoordinates->text(), latitude, longitude))
		return;
	reverseGeoLookup(latitude, longitude, &taxonomy);
	ui.locationTags->setText(constructLocationTags(&taxonomy, false));
}

void LocationInformationWidget::updateLocationOnMap()
{
	if (!diveSite)
		return;
	degrees_t latitude, longitude;
	if (!parseGpsText(ui.diveSiteCoordinates->text(), latitude, longitude))
		return;
	MapWidget::instance()->updateDiveSiteCoordinates(diveSite->uuid, latitude, longitude);
	filter_model.setCoordinates(latitude, longitude);
}

DiveLocationFilterProxyModel::DiveLocationFilterProxyModel(QObject*)
{
}

DiveLocationLineEdit *location_line_edit = 0;

bool DiveLocationFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex&) const
{
	if (source_row == 0)
		return true;

	QString sourceString = sourceModel()->index(source_row, LocationInformationModel::NAME).data(Qt::DisplayRole).toString();
	return sourceString.toLower().contains(location_line_edit->text().toLower());
}

bool DiveLocationFilterProxyModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
	return source_left.data().toString() < source_right.data().toString();
}

DiveLocationModel::DiveLocationModel(QObject*)
{
	resetModel();
}

void DiveLocationModel::resetModel()
{
	beginResetModel();
	endResetModel();
}

QVariant DiveLocationModel::data(const QModelIndex &index, int role) const
{
	static const QIcon plusIcon(":list-add-icon");
	static const QIcon geoCode(":geotag-icon");

	if (index.row() <= 1) { // two special cases.
		if (index.column() == LocationInformationModel::UUID) {
			return RECENTLY_ADDED_DIVESITE;
		}
		switch (role) {
		case Qt::DisplayRole:
			return new_ds_value[index.row()];
		case Qt::ToolTipRole:
			return displayed_dive.dive_site_uuid ?
				tr("Create a new dive site, copying relevant information from the current dive.") :
				tr("Create a new dive site with this name");
		case Qt::DecorationRole:
			return plusIcon;
		}
	}

	// The dive sites are -2 because of the first two items.
	struct dive_site *ds = get_dive_site(index.row() - 2);
	return LocationInformationModel::getDiveSiteData(ds, index.column(), role);
}

int DiveLocationModel::columnCount(const QModelIndex&) const
{
	return LocationInformationModel::COLUMNS;
}

int DiveLocationModel::rowCount(const QModelIndex&) const
{
	return dive_site_table.nr + 2;
}

bool DiveLocationModel::setData(const QModelIndex &index, const QVariant &value, int)
{
	if (!index.isValid())
		return false;
	if (index.row() > 1)
		return false;

	new_ds_value[index.row()] = value.toString();

	dataChanged(index, index);
	return true;
}

DiveLocationLineEdit::DiveLocationLineEdit(QWidget *parent) : QLineEdit(parent),
							      proxy(new DiveLocationFilterProxyModel()),
							      model(new DiveLocationModel()),
							      view(new DiveLocationListView()),
							      currType(NO_DIVE_SITE)
{
	currUuid = 0;
	location_line_edit = this;

	proxy->setSourceModel(model);
	proxy->setFilterKeyColumn(LocationInformationModel::NAME);

	view->setModel(proxy);
	view->setModelColumn(LocationInformationModel::NAME);
	view->setItemDelegate(new LocationFilterDelegate());
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	view->setSelectionBehavior(QAbstractItemView::SelectRows);
	view->setSelectionMode(QAbstractItemView::SingleSelection);
	view->setParent(0, Qt::Popup);
	view->installEventFilter(this);
	view->setFocusPolicy(Qt::NoFocus);
	view->setFocusProxy(this);
	view->setMouseTracking(true);

	connect(this, &QLineEdit::textEdited, this, &DiveLocationLineEdit::setTemporaryDiveSiteName);
	connect(view, &QAbstractItemView::activated, this, &DiveLocationLineEdit::itemActivated);
	connect(view, &QAbstractItemView::entered, this, &DiveLocationLineEdit::entered);
	connect(view, &DiveLocationListView::currentIndexChanged, this, &DiveLocationLineEdit::currentChanged);
}

bool DiveLocationLineEdit::eventFilter(QObject*, QEvent *e)
{
	if (e->type() == QEvent::KeyPress) {
		QKeyEvent *keyEv = (QKeyEvent *)e;

		if (keyEv->key() == Qt::Key_Escape) {
			view->hide();
			return true;
		}

		if (keyEv->key() == Qt::Key_Return ||
		    keyEv->key() == Qt::Key_Enter) {
#if __APPLE__
			// for some reason it seems like on a Mac hitting return/enter
			// doesn't call 'activated' for that index. so let's do it manually
			if (view->currentIndex().isValid())
				itemActivated(view->currentIndex());
#endif
			view->hide();
			return false;
		}

		if (keyEv->key() == Qt::Key_Tab) {
			itemActivated(view->currentIndex());
			view->hide();
			return false;
		}
		event(e);
	} else if (e->type() == QEvent::MouseButtonPress) {
		if (!view->underMouse()) {
			view->hide();
			return true;
		}
	}
	else if (e->type() == QEvent::InputMethod) {
		this->inputMethodEvent(static_cast<QInputMethodEvent *>(e));
	}

	return false;
}

void DiveLocationLineEdit::focusOutEvent(QFocusEvent *ev)
{
	if (!view->isVisible()) {
		QLineEdit::focusOutEvent(ev);
	}
}

void DiveLocationLineEdit::itemActivated(const QModelIndex &index)
{
	QModelIndex idx = index;
	if (index.column() == LocationInformationModel::UUID)
		idx = index.model()->index(index.row(), LocationInformationModel::NAME);

	QModelIndex uuidIndex = index.model()->index(index.row(), LocationInformationModel::UUID);
	uint32_t uuid = uuidIndex.data().toInt();
	currType = uuid == 1 ? NEW_DIVE_SITE : EXISTING_DIVE_SITE;
	currUuid = uuid;
	setText(idx.data().toString());
	if (currUuid == NEW_DIVE_SITE)
		qDebug() << "Setting a New dive site";
	else
		qDebug() << "Setting a Existing dive site";
	if (view->isVisible())
		view->hide();
	emit diveSiteSelected(currUuid);
}

void DiveLocationLineEdit::refreshDiveSiteCache()
{
	model->resetModel();
}

static struct dive_site *get_dive_site_name_start_which_str(const QString &str)
{
	struct dive_site *ds;
	int i;
	for_each_dive_site (i, ds) {
		QString dsName(ds->name);
		if (dsName.toLower().startsWith(str.toLower())) {
			return ds;
		}
	}
	return NULL;
}

void DiveLocationLineEdit::setTemporaryDiveSiteName(const QString&)
{
	// This function fills the first two entries with potential names of
	// a dive site to be generated. The first entry is simply the entered
	// text. The second entry is the first known dive site name starting
	// with the entered text.
	QModelIndex i0 = model->index(0, LocationInformationModel::NAME);
	QModelIndex i1 = model->index(1, LocationInformationModel::NAME);
	model->setData(i0, text());

	// Note: if i1_name stays empty, the line will automatically
	// be filtered out by the proxy filter, as it does not contain
	// the user entered text.
	QString i1_name;
	if (struct dive_site *ds = get_dive_site_name_start_which_str(text())) {
		const QString orig_name = QString(ds->name).toLower();
		const QString new_name = text().toLower();
		if (new_name != orig_name)
			i1_name = QString(ds->name);
	}

	model->setData(i1, i1_name);
	proxy->invalidate();
	fixPopupPosition();
	if (!view->isVisible())
		view->show();
}

void DiveLocationLineEdit::keyPressEvent(QKeyEvent *ev)
{
	QLineEdit::keyPressEvent(ev);
	if (ev->key() != Qt::Key_Left &&
	    ev->key() != Qt::Key_Right &&
	    ev->key() != Qt::Key_Escape &&
	    ev->key() != Qt::Key_Return) {

		if (ev->key() != Qt::Key_Up && ev->key() != Qt::Key_Down) {
			currType = NEW_DIVE_SITE;
			currUuid = RECENTLY_ADDED_DIVESITE;
		} else {
			showPopup();
		}
	} else if (ev->key() == Qt::Key_Escape) {
		view->hide();
	}
}

void DiveLocationLineEdit::fixPopupPosition()
{
	const QRect screen = QApplication::desktop()->availableGeometry(this);
	const int maxVisibleItems = 5;
	QPoint pos;
	int rh, w;
	int h = (view->sizeHintForRow(0) * qMin(maxVisibleItems, view->model()->rowCount()) + 3) + 3;
	QScrollBar *hsb = view->horizontalScrollBar();
	if (hsb && hsb->isVisible())
		h += view->horizontalScrollBar()->sizeHint().height();

	rh = height();
	pos = mapToGlobal(QPoint(0, height() - 2));
	w = width();

	if (w > screen.width())
		w = screen.width();
	if ((pos.x() + w) > (screen.x() + screen.width()))
		pos.setX(screen.x() + screen.width() - w);
	if (pos.x() < screen.x())
		pos.setX(screen.x());

	int top = pos.y() - rh - screen.top() + 2;
	int bottom = screen.bottom() - pos.y();
	h = qMax(h, view->minimumHeight());
	if (h > bottom) {
		h = qMin(qMax(top, bottom), h);
		if (top > bottom)
			pos.setY(pos.y() - h - rh + 2);
	}

	view->setGeometry(pos.x(), pos.y(), w, h);
	if (!view->currentIndex().isValid() && view->model()->rowCount()) {
		view->setCurrentIndex(view->model()->index(0, 1));
	}
}

void DiveLocationLineEdit::setCurrentDiveSiteUuid(uint32_t uuid)
{
	currUuid = uuid;
	if (uuid == 0) {
		currType = NO_DIVE_SITE;
	}
	struct dive_site *ds = get_dive_site_by_uuid(uuid);
	if (!ds)
		clear();
	else
		setText(ds->name);
}

void DiveLocationLineEdit::showPopup()
{
	fixPopupPosition();
	if (!view->isVisible()) {
		setTemporaryDiveSiteName(text());
		proxy->invalidate();
		view->show();
	}
}

DiveLocationLineEdit::DiveSiteType DiveLocationLineEdit::currDiveSiteType() const
{
	return currType;
}

uint32_t DiveLocationLineEdit::currDiveSiteUuid() const
{
	return currUuid;
}

DiveLocationListView::DiveLocationListView(QWidget*)
{
}

void DiveLocationListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
	QListView::currentChanged(current, previous);
	emit currentIndexChanged(current);
}
