/*
 * maintab.cpp
 *
 * classes for the "notebook" area of the main window of Subsurface
 *
 */
#include "desktop-widgets/maintab.h"
#include "desktop-widgets/mainwindow.h"
#include "desktop-widgets/globe.h"
#include "core/helpers.h"
#include "core/statistics.h"
#include "desktop-widgets/modeldelegates.h"
#include "qt-models/diveplannermodel.h"
#include "desktop-widgets/divelistview.h"
#include "core/display.h"
#include "profile-widget/profilewidget2.h"
#include "desktop-widgets/diveplanner.h"
#include "core/divesitehelpers.h"
#include "qt-models/cylindermodel.h"
#include "qt-models/weightmodel.h"
#include "qt-models/divepicturemodel.h"
#include "qt-models/divecomputerextradatamodel.h"
#include "qt-models/divelocationmodel.h"
#include "core/divesite.h"
#include "desktop-widgets/locationinformation.h"

#include <QCompleter>
#include <QSettings>
#include <QScrollBar>
#include <QShortcut>
#include <QMessageBox>
#include <QDesktopServices>
#include <QStringList>

MainTab::MainTab(QWidget *parent) : QTabWidget(parent),
	weightModel(new WeightModel(this)),
	cylindersModel(new CylindersModel(this)),
	extraDataModel(new ExtraDataModel(this)),
	editMode(NONE),
	divePictureModel(DivePictureModel::instance()),
	copyPaste(false),
	currentTrip(0)
{
	ui.setupUi(this);
	ui.dateEdit->setDisplayFormat(prefs.date_format);

	memset(&displayed_dive, 0, sizeof(displayed_dive));
	memset(&displayedTrip, 0, sizeof(displayedTrip));

	ui.cylinders->setModel(cylindersModel);
	ui.weights->setModel(weightModel);
	ui.photosView->setModel(divePictureModel);
	connect(ui.photosView, SIGNAL(photoDoubleClicked(QString)), this, SLOT(photoDoubleClicked(QString)));
	ui.extraData->setModel(extraDataModel);
	closeMessage();

	connect(ui.editDiveSiteButton, SIGNAL(clicked()), MainWindow::instance(), SIGNAL(startDiveSiteEdit()));
#ifndef NO_MARBLE
	connect(ui.location, &DiveLocationLineEdit::entered, GlobeGPS::instance(), &GlobeGPS::centerOnIndex);
	connect(ui.location, &DiveLocationLineEdit::currentChanged, GlobeGPS::instance(), &GlobeGPS::centerOnIndex);
#endif

	QAction *action = new QAction(tr("Apply changes"), this);
	connect(action, SIGNAL(triggered(bool)), this, SLOT(acceptChanges()));
	addMessageAction(action);

	action = new QAction(tr("Discard changes"), this);
	connect(action, SIGNAL(triggered(bool)), this, SLOT(rejectChanges()));
	addMessageAction(action);

	QShortcut *closeKey = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(closeKey, SIGNAL(activated()), this, SLOT(escDetected()));

	if (qApp->style()->objectName() == "oxygen")
		setDocumentMode(true);
	else
		setDocumentMode(false);

	// we start out with the fields read-only; once things are
	// filled from a dive, they are made writeable
	setEnabled(false);

	Q_FOREACH (QObject *obj, ui.statisticsTab->children()) {
		QLabel *label = qobject_cast<QLabel *>(obj);
		if (label)
			label->setAlignment(Qt::AlignHCenter);
	}
	ui.cylinders->setTitle(tr("Cylinders"));
	ui.cylinders->setBtnToolTip(tr("Add cylinder"));
	connect(ui.cylinders, SIGNAL(addButtonClicked()), this, SLOT(addCylinder_clicked()));

	ui.weights->setTitle(tr("Weights"));
	ui.weights->setBtnToolTip(tr("Add weight system"));
	connect(ui.weights, SIGNAL(addButtonClicked()), this, SLOT(addWeight_clicked()));

	// This needs to be the same order as enum dive_comp_type in dive.h!
	ui.DiveType->insertItems(0, QStringList() << tr("OC") << tr("CCR") << tr("pSCR") << tr("Freedive"));
	connect(ui.DiveType, SIGNAL(currentIndexChanged(int)), this, SLOT(divetype_Changed(int)));

	connect(ui.cylinders->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editCylinderWidget(QModelIndex)));
	connect(ui.weights->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editWeightWidget(QModelIndex)));

	ui.cylinders->view()->setItemDelegateForColumn(CylindersModel::TYPE, new TankInfoDelegate(this));
	ui.cylinders->view()->setItemDelegateForColumn(CylindersModel::USE, new TankUseDelegate(this));
	ui.weights->view()->setItemDelegateForColumn(WeightModel::TYPE, new WSInfoDelegate(this));
	ui.cylinders->view()->setColumnHidden(CylindersModel::DEPTH, true);
	completers.buddy = new QCompleter(&buddyModel, ui.buddy);
	completers.divemaster = new QCompleter(&diveMasterModel, ui.divemaster);
	completers.suit = new QCompleter(&suitModel, ui.suit);
	completers.tags = new QCompleter(&tagModel, ui.tagWidget);
	completers.buddy->setCaseSensitivity(Qt::CaseInsensitive);
	completers.divemaster->setCaseSensitivity(Qt::CaseInsensitive);
	completers.suit->setCaseSensitivity(Qt::CaseInsensitive);
	completers.tags->setCaseSensitivity(Qt::CaseInsensitive);
	ui.buddy->setCompleter(completers.buddy);
	ui.divemaster->setCompleter(completers.divemaster);
	ui.suit->setCompleter(completers.suit);
	ui.tagWidget->setCompleter(completers.tags);
	ui.diveNotesMessage->hide();
	ui.diveEquipmentMessage->hide();
	ui.diveInfoMessage->hide();
	ui.diveStatisticsMessage->hide();
	ui.depth->hide();
	ui.depthLabel->hide();
	ui.duration->hide();
	ui.durationLabel->hide();
	setMinimumHeight(0);
	setMinimumWidth(0);

	// Current display of things on Gnome3 looks like shit, so
	// let`s fix that.
	if (isGnome3Session()) {
		QPalette p;
		p.setColor(QPalette::Window, QColor(Qt::white));
		ui.scrollArea->viewport()->setPalette(p);
		ui.scrollArea_2->viewport()->setPalette(p);
		ui.scrollArea_3->viewport()->setPalette(p);
		ui.scrollArea_4->viewport()->setPalette(p);

		// GroupBoxes in Gnome3 looks like I'v drawn them...
		static const QString gnomeCss(
			"QGroupBox {"
			"    background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
			"    stop: 0 #E0E0E0, stop: 1 #FFFFFF);"
			"    border: 2px solid gray;"
			"    border-radius: 5px;"
			"    margin-top: 1ex;"
			"}"
			"QGroupBox::title {"
			"    subcontrol-origin: margin;"
			"    subcontrol-position: top center;"
			"    padding: 0 3px;"
			"    background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
			"    stop: 0 #E0E0E0, stop: 1 #FFFFFF);"
			"}");
		Q_FOREACH (QGroupBox *box, findChildren<QGroupBox *>()) {
			box->setStyleSheet(gnomeCss);
		}
	}
	// QLineEdit and QLabels should have minimal margin on the left and right but not waste vertical space
	QMargins margins(3, 2, 1, 0);
	Q_FOREACH (QLabel *label, findChildren<QLabel *>()) {
		label->setContentsMargins(margins);
	}
	ui.cylinders->view()->horizontalHeader()->setContextMenuPolicy(Qt::ActionsContextMenu);
	ui.weights->view()->horizontalHeader()->setContextMenuPolicy(Qt::ActionsContextMenu);

	QSettings s;
	s.beginGroup("cylinders_dialog");
	for (int i = 0; i < CylindersModel::COLUMNS; i++) {
		if ((i == CylindersModel::REMOVE) || (i == CylindersModel::TYPE))
			continue;
		bool checked = s.value(QString("column%1_hidden").arg(i)).toBool();
		action = new QAction(cylindersModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString(), ui.cylinders->view());
		action->setCheckable(true);
		action->setData(i);
		action->setChecked(!checked);
		connect(action, SIGNAL(triggered(bool)), this, SLOT(toggleTriggeredColumn()));
		ui.cylinders->view()->setColumnHidden(i, checked);
		ui.cylinders->view()->horizontalHeader()->addAction(action);
	}

	ui.waitingSpinner->setRoundness(70.0);
	ui.waitingSpinner->setMinimumTrailOpacity(15.0);
	ui.waitingSpinner->setTrailFadePercentage(70.0);
	ui.waitingSpinner->setNumberOfLines(8);
	ui.waitingSpinner->setLineLength(5);
	ui.waitingSpinner->setLineWidth(3);
	ui.waitingSpinner->setInnerRadius(5);
	ui.waitingSpinner->setRevolutionsPerSecond(1);

	connect(ReverseGeoLookupThread::instance(), SIGNAL(finished()),
			LocationInformationModel::instance(), SLOT(update()));

	connect(ReverseGeoLookupThread::instance(), &QThread::finished,
			this, &MainTab::setCurrentLocationIndex);

	connect(ui.diveNotesMessage, &KMessageWidget::showAnimationFinished,
					ui.location, &DiveLocationLineEdit::fixPopupPosition);

	// enable URL clickability in notes:
	new TextHyperlinkEventFilter(ui.notes);//destroyed when ui.notes is destroyed

	acceptingEdit = false;

	ui.diveTripLocation->hide();
	ui.photosView->setSelectionMode(QAbstractItemView::MultiSelection);
}

MainTab::~MainTab()
{
	QSettings s;
	s.beginGroup("cylinders_dialog");
	for (int i = 0; i < CylindersModel::COLUMNS; i++) {
		if ((i == CylindersModel::REMOVE) || (i == CylindersModel::TYPE))
			continue;
		s.setValue(QString("column%1_hidden").arg(i), ui.cylinders->view()->isColumnHidden(i));
	}
}

void MainTab::setCurrentLocationIndex()
{
	if (current_dive) {
		struct dive_site *ds = get_dive_site_by_uuid(current_dive->dive_site_uuid);
		if (ds)
			ui.location->setCurrentDiveSiteUuid(ds->uuid);
		else
			ui.location->clear();
	}
}

void MainTab::enableGeoLookupEdition()
{
	ui.waitingSpinner->stop();
}

void MainTab::disableGeoLookupEdition()
{
	ui.waitingSpinner->start();
}

void MainTab::toggleTriggeredColumn()
{
	QAction *action = qobject_cast<QAction *>(sender());
	int col = action->data().toInt();
	QTableView *view = ui.cylinders->view();

	if (action->isChecked()) {
		view->showColumn(col);
		if (view->columnWidth(col) <= 15)
			view->setColumnWidth(col, 80);
	} else
		view->hideColumn(col);
}

void MainTab::addDiveStarted()
{
	enableEdition(ADD);
}

void MainTab::addMessageAction(QAction *action)
{
	ui.diveEquipmentMessage->addAction(action);
	ui.diveNotesMessage->addAction(action);
	ui.diveInfoMessage->addAction(action);
	ui.diveStatisticsMessage->addAction(action);
}

void MainTab::hideMessage()
{
	ui.diveNotesMessage->animatedHide();
	ui.diveEquipmentMessage->animatedHide();
	ui.diveInfoMessage->animatedHide();
	ui.diveStatisticsMessage->animatedHide();
	updateTextLabels(false);
}

void MainTab::closeMessage()
{
	hideMessage();
	ui.diveNotesMessage->setCloseButtonVisible(false);
	ui.diveEquipmentMessage->setCloseButtonVisible(false);
	ui.diveInfoMessage->setCloseButtonVisible(false);
	ui.diveStatisticsMessage->setCloseButtonVisible(false);
}

void MainTab::displayMessage(QString str)
{
	ui.diveNotesMessage->setCloseButtonVisible(false);
	ui.diveEquipmentMessage->setCloseButtonVisible(false);
	ui.diveInfoMessage->setCloseButtonVisible(false);
	ui.diveStatisticsMessage->setCloseButtonVisible(false);
	ui.diveNotesMessage->setText(str);
	ui.diveNotesMessage->animatedShow();
	ui.diveEquipmentMessage->setText(str);
	ui.diveEquipmentMessage->animatedShow();
	ui.diveInfoMessage->setText(str);
	ui.diveInfoMessage->animatedShow();
	ui.diveStatisticsMessage->setText(str);
	ui.diveStatisticsMessage->animatedShow();
	updateTextLabels();
}

void MainTab::updateTextLabels(bool showUnits)
{
	if (showUnits) {
		ui.airTempLabel->setText(tr("Air temp. [%1]").arg(get_temp_unit()));
		ui.waterTempLabel->setText(tr("Water temp. [%1]").arg(get_temp_unit()));
	} else {
		ui.airTempLabel->setText(tr("Air temp."));
		ui.waterTempLabel->setText(tr("Water temp."));
	}
}

void MainTab::enableEdition(EditMode newEditMode)
{
	const bool isTripEdit = MainWindow::instance() &&
		MainWindow::instance()->dive_list()->selectedTrips().count() == 1;

	if (((newEditMode == DIVE || newEditMode == NONE) && current_dive == NULL) || editMode != NONE)
		return;
	modified = false;
	copyPaste = false;
	if ((newEditMode == DIVE || newEditMode == NONE) &&
	    !isTripEdit &&
	    current_dive->dc.model &&
	    strcmp(current_dive->dc.model, "manually added dive") == 0) {
		// editCurrentDive will call enableEdition with newEditMode == MANUALLY_ADDED_DIVE
		// so exit this function here after editCurrentDive() returns



		// FIXME : can we get rid of this recursive crap?



		MainWindow::instance()->editCurrentDive();
		return;
	}

	ui.editDiveSiteButton->setEnabled(false);
	MainWindow::instance()->dive_list()->setEnabled(false);
	MainWindow::instance()->setEnabledToolbar(false);

	if (isTripEdit) {
		// we are editing trip location and notes
		displayMessage(tr("This trip is being edited."));
		currentTrip = current_dive->divetrip;
		ui.dateEdit->setEnabled(false);
		editMode = TRIP;
	} else {
		ui.dateEdit->setEnabled(true);
		if (amount_selected > 1) {
			displayMessage(tr("Multiple dives are being edited."));
		} else {
			displayMessage(tr("This dive is being edited."));
		}
		editMode = newEditMode != NONE ? newEditMode : DIVE;
	}
}

void MainTab::clearEquipment()
{
	cylindersModel->clear();
	weightModel->clear();
}

void MainTab::nextInputField(QKeyEvent *event)
{
	keyPressEvent(event);
}

void MainTab::clearInfo()
{
	ui.sacText->clear();
	ui.otuText->clear();
	ui.maxcnsText->clear();
	ui.oxygenHeliumText->clear();
	ui.gasUsedText->clear();
	ui.dateText->clear();
	ui.diveTimeText->clear();
	ui.surfaceIntervalText->clear();
	ui.maximumDepthText->clear();
	ui.averageDepthText->clear();
	ui.waterTemperatureText->clear();
	ui.airTemperatureText->clear();
	ui.airPressureText->clear();
	ui.salinityText->clear();
	ui.tagWidget->clear();
}

void MainTab::clearStats()
{
	ui.depthLimits->clear();
	ui.sacLimits->clear();
	ui.divesAllText->clear();
	ui.tempLimits->clear();
	ui.totalTimeAllText->clear();
	ui.timeLimits->clear();
}

#define UPDATE_TEXT(d, field)          \
	if (clear || !d.field)         \
		ui.field->setText(QString()); \
	else                           \
		ui.field->setText(d.field)

#define UPDATE_TEMP(d, field)            \
	if (clear || d.field.mkelvin == 0) \
		ui.field->setText("");   \
	else                             \
		ui.field->setText(get_temperature_string(d.field, true))

bool MainTab::isEditing()
{
	return editMode != NONE;
}

void MainTab::showLocation()
{
	if (get_dive_site_by_uuid(displayed_dive.dive_site_uuid))
		ui.location->setCurrentDiveSiteUuid(displayed_dive.dive_site_uuid);
	else
		ui.location->clear();
}

// Seems wrong, since we can also call updateDiveInfo(), but since the updateDiveInfo
// has a parameter on it's definition it didn't worked on the signal slot connection.
void MainTab::refreshDiveInfo()
{
	updateDiveInfo();
}

void MainTab::updateDepthDuration()
{
	ui.depth->setVisible(true);
	ui.depthLabel->setVisible(true);
	ui.duration->setVisible(true);
	ui.durationLabel->setVisible(true);
	ui.duration->setText(QDateTime::fromTime_t(displayed_dive.duration.seconds).toUTC().toString("h:mm"));
	ui.depth->setText(get_depth_string(displayed_dive.maxdepth, true));
}

void MainTab::updateDiveInfo(bool clear)
{
	ui.location->refreshDiveSiteCache();
	EditMode rememberEM = editMode;
	// don't execute this while adding / planning a dive
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE || MainWindow::instance()->graphics()->isPlanner())
		return;
	if (!isEnabled() && !clear )
		setEnabled(true);
	if (isEnabled() && clear)
		setEnabled(false);
	editMode = IGNORE; // don't trigger on changes to the widgets

	// This method updates ALL tabs whenever a new dive or trip is
	// selected.
	// If exactly one trip has been selected, we show the location / notes
	// for the trip in the Info tab, otherwise we show the info of the
	// selected_dive
	temperature_t temp;
	struct dive *prevd;
	char buf[1024];

	process_selected_dives();
	process_all_dives(&displayed_dive, &prevd);

	divePictureModel->updateDivePictures();

	ui.notes->setText(QString());
	if (!clear) {
		QString tmp(displayed_dive.notes);
		if (tmp.indexOf("<table") != -1)
			ui.notes->setHtml(tmp);
		else
			ui.notes->setPlainText(tmp);
	}
	UPDATE_TEXT(displayed_dive, notes);
	UPDATE_TEXT(displayed_dive, suit);
	UPDATE_TEXT(displayed_dive, divemaster);
	UPDATE_TEXT(displayed_dive, buddy);
	UPDATE_TEMP(displayed_dive, airtemp);
	UPDATE_TEMP(displayed_dive, watertemp);
	ui.DiveType->setCurrentIndex(get_dive_dc(&displayed_dive, dc_number)->divemode);

	if (!clear) {
		struct dive_site *ds = NULL;
		// if we are showing a dive and editing it, let's refer to the displayed_dive_site as that
		// already may contain changes, otherwise start with the dive site referred to by the displayed
		// dive
		if (rememberEM == DIVE) {
			ds = &displayed_dive_site;
		} else {
			ds = get_dive_site_by_uuid(displayed_dive.dive_site_uuid);
			if (ds)
				copy_dive_site(ds, &displayed_dive_site);
		}

		if (ds) {
			ui.location->setCurrentDiveSiteUuid(ds->uuid);
			ui.locationTags->setText(constructLocationTags(ds->uuid));
		} else {
			ui.location->clear();
			clear_dive_site(&displayed_dive_site);
		}

		// Subsurface always uses "local time" as in "whatever was the local time at the location"
		// so all time stamps have no time zone information and are in UTC
		QDateTime localTime = QDateTime::fromMSecsSinceEpoch(1000*displayed_dive.when, Qt::UTC);
		localTime.setTimeSpec(Qt::UTC);
		ui.dateEdit->setDate(localTime.date());
		ui.timeEdit->setTime(localTime.time());
		if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
			setTabText(0, tr("Trip notes"));
			currentTrip = *MainWindow::instance()->dive_list()->selectedTrips().begin();
			// only use trip relevant fields
			ui.divemaster->setVisible(false);
			ui.DivemasterLabel->setVisible(false);
			ui.buddy->setVisible(false);
			ui.BuddyLabel->setVisible(false);
			ui.suit->setVisible(false);
			ui.SuitLabel->setVisible(false);
			ui.rating->setVisible(false);
			ui.RatingLabel->setVisible(false);
			ui.visibility->setVisible(false);
			ui.visibilityLabel->setVisible(false);
			ui.tagWidget->setVisible(false);
			ui.TagLabel->setVisible(false);
			ui.airTempLabel->setVisible(false);
			ui.airtemp->setVisible(false);
			ui.DiveType->setVisible(false);
			ui.TypeLabel->setVisible(false);
			ui.waterTempLabel->setVisible(false);
			ui.watertemp->setVisible(false);
			ui.dateEdit->setReadOnly(true);
			ui.label->setVisible(false);
			ui.timeEdit->setVisible(false);
			ui.diveTripLocation->show();
			ui.location->hide();
			ui.editDiveSiteButton->hide();
			// rename the remaining fields and fill data from selected trip
			ui.LocationLabel->setText(tr("Trip location"));
			ui.diveTripLocation->setText(currentTrip->location);
			ui.locationTags->clear();
			//TODO: Fix this.
			//ui.location->setText(currentTrip->location);
			ui.NotesLabel->setText(tr("Trip notes"));
			ui.notes->setText(currentTrip->notes);
			clearEquipment();
			ui.equipmentTab->setEnabled(false);
			ui.depth->setVisible(false);
			ui.depthLabel->setVisible(false);
			ui.duration->setVisible(false);
			ui.durationLabel->setVisible(false);
		} else {
			setTabText(0, tr("Notes"));
			currentTrip = NULL;
			// make all the fields visible writeable
			ui.diveTripLocation->hide();
			ui.location->show();
			ui.editDiveSiteButton->show();
			ui.divemaster->setVisible(true);
			ui.buddy->setVisible(true);
			ui.suit->setVisible(true);
			ui.SuitLabel->setVisible(true);
			ui.rating->setVisible(true);
			ui.RatingLabel->setVisible(true);
			ui.visibility->setVisible(true);
			ui.visibilityLabel->setVisible(true);
			ui.BuddyLabel->setVisible(true);
			ui.DivemasterLabel->setVisible(true);
			ui.TagLabel->setVisible(true);
			ui.tagWidget->setVisible(true);
			ui.airTempLabel->setVisible(true);
			ui.airtemp->setVisible(true);
			ui.TypeLabel->setVisible(true);
			ui.DiveType->setVisible(true);
			ui.waterTempLabel->setVisible(true);
			ui.watertemp->setVisible(true);
			ui.dateEdit->setReadOnly(false);
			ui.label->setVisible(true);
			ui.timeEdit->setVisible(true);
			/* and fill them from the dive */
			ui.rating->setCurrentStars(displayed_dive.rating);
			ui.visibility->setCurrentStars(displayed_dive.visibility);
			// reset labels in case we last displayed trip notes
			ui.LocationLabel->setText(tr("Location"));
			ui.NotesLabel->setText(tr("Notes"));
			ui.equipmentTab->setEnabled(true);
			cylindersModel->updateDive();
			weightModel->updateDive();
			extraDataModel->updateDive();
			taglist_get_tagstring(displayed_dive.tag_list, buf, 1024);
			ui.tagWidget->setText(QString(buf));
			bool isManual = !current_dive || same_string(current_dive->dc.model, "manually added dive");
			ui.depth->setVisible(isManual);
			ui.depthLabel->setVisible(isManual);
			ui.duration->setVisible(isManual);
			ui.durationLabel->setVisible(isManual);
		}
		ui.duration->setText(QDateTime::fromTime_t(displayed_dive.duration.seconds).toUTC().toString("h:mm"));
		ui.depth->setText(get_depth_string(displayed_dive.maxdepth, true));
		ui.maximumDepthText->setText(get_depth_string(displayed_dive.maxdepth, true));
		ui.averageDepthText->setText(get_depth_string(displayed_dive.meandepth, true));
		ui.maxcnsText->setText(QString("%1\%").arg(displayed_dive.maxcns));
		ui.otuText->setText(QString("%1").arg(displayed_dive.otu));
		ui.waterTemperatureText->setText(get_temperature_string(displayed_dive.watertemp, true));
		ui.airTemperatureText->setText(get_temperature_string(displayed_dive.airtemp, true));
		ui.DiveType->setCurrentIndex(get_dive_dc(&displayed_dive, dc_number)->divemode);

		volume_t gases[MAX_CYLINDERS] = {};
		get_gas_used(&displayed_dive, gases);
		QString volumes;
		int mean[MAX_CYLINDERS], duration[MAX_CYLINDERS];
		per_cylinder_mean_depth(&displayed_dive, select_dc(&displayed_dive), mean, duration);
		volume_t sac;
		QString gaslist, SACs, separator;

		gaslist = ""; SACs = ""; volumes = ""; separator = "";
		for (int i = 0; i < MAX_CYLINDERS; i++) {
			if (!is_cylinder_used(&displayed_dive, i))
				continue;
			gaslist.append(separator); volumes.append(separator); SACs.append(separator);
			separator = "\n";

			gaslist.append(gasname(&displayed_dive.cylinder[i].gasmix));
			if (!gases[i].mliter)
				continue;
			volumes.append(get_volume_string(gases[i], true));
			if (duration[i]) {
				sac.mliter = gases[i].mliter / (depth_to_atm(mean[i], &displayed_dive) * duration[i] / 60);
				SACs.append(get_volume_string(sac, true).append(tr("/min")));
			}
		}
		ui.gasUsedText->setText(volumes);
		ui.oxygenHeliumText->setText(gaslist);
		ui.dateText->setText(get_short_dive_date_string(displayed_dive.when));
		if (displayed_dive.dc.divemode != FREEDIVE)
				ui.diveTimeText->setText(get_time_string_s(displayed_dive.duration.seconds + 30, 0, false));
			else
				ui.diveTimeText->setText(get_time_string_s(displayed_dive.duration.seconds, 0, true));
		if (prevd)
			ui.surfaceIntervalText->setText(get_time_string_s(displayed_dive.when - (prevd->when + prevd->duration.seconds), 4,
									  (displayed_dive.dc.divemode == FREEDIVE)));
		else
			ui.surfaceIntervalText->clear();
		if (mean[0])
			ui.sacText->setText(SACs);
		else
			ui.sacText->clear();
		if (displayed_dive.surface_pressure.mbar)
			/* this is ALWAYS displayed in mbar */
			ui.airPressureText->setText(QString("%1mbar").arg(displayed_dive.surface_pressure.mbar));
		else
			ui.airPressureText->clear();
		if (displayed_dive.salinity)
			ui.salinityText->setText(QString("%1g/l").arg(displayed_dive.salinity / 10.0));
		else
			ui.salinityText->clear();
		ui.depthLimits->setMaximum(get_depth_string(stats_selection.max_depth, true));
		ui.depthLimits->setMinimum(get_depth_string(stats_selection.min_depth, true));
		// the overall average depth is really confusing when listed between the
		// deepest and shallowest dive - let's just not set it
		// ui.depthLimits->setAverage(get_depth_string(stats_selection.avg_depth, true));
		ui.depthLimits->overrideMaxToolTipText(tr("Deepest dive"));
		ui.depthLimits->overrideMinToolTipText(tr("Shallowest dive"));
		if (amount_selected > 1 && stats_selection.max_sac.mliter)
			ui.sacLimits->setMaximum(get_volume_string(stats_selection.max_sac, true).append(tr("/min")));
		else
			ui.sacLimits->setMaximum("");
		if (amount_selected > 1 && stats_selection.min_sac.mliter)
			ui.sacLimits->setMinimum(get_volume_string(stats_selection.min_sac, true).append(tr("/min")));
		else
			ui.sacLimits->setMinimum("");
		if (stats_selection.avg_sac.mliter)
			ui.sacLimits->setAverage(get_volume_string(stats_selection.avg_sac, true).append(tr("/min")));
		else
			ui.sacLimits->setAverage("");
		ui.sacLimits->overrideMaxToolTipText(tr("Highest total SAC of a dive"));
		ui.sacLimits->overrideMinToolTipText(tr("Lowest total SAC of a dive"));
		ui.sacLimits->overrideAvgToolTipText(tr("Average total SAC of all selected dives"));
		ui.divesAllText->setText(QString::number(stats_selection.selection_size));
		temp.mkelvin = stats_selection.max_temp;
		ui.tempLimits->setMaximum(get_temperature_string(temp, true));
		temp.mkelvin = stats_selection.min_temp;
		ui.tempLimits->setMinimum(get_temperature_string(temp, true));
		if (stats_selection.combined_temp && stats_selection.combined_count) {
			const char *unit;
			get_temp_units(0, &unit);
			ui.tempLimits->setAverage(QString("%1%2").arg(stats_selection.combined_temp / stats_selection.combined_count, 0, 'f', 1).arg(unit));
		}
		ui.tempLimits->overrideMaxToolTipText(tr("Highest temperature"));
		ui.tempLimits->overrideMinToolTipText(tr("Lowest temperature"));
		ui.tempLimits->overrideAvgToolTipText(tr("Average temperature of all selected dives"));
		ui.totalTimeAllText->setText(get_time_string_s(stats_selection.total_time.seconds, 0, (displayed_dive.dc.divemode == FREEDIVE)));
		int seconds = stats_selection.total_time.seconds;
		if (stats_selection.selection_size)
			seconds /= stats_selection.selection_size;
		ui.timeLimits->setAverage(get_time_string_s(seconds, 0,(displayed_dive.dc.divemode == FREEDIVE)));
		if (amount_selected > 1) {
			ui.timeLimits->setMaximum(get_time_string_s(stats_selection.longest_time.seconds, 0, (displayed_dive.dc.divemode == FREEDIVE)));
			ui.timeLimits->setMinimum(get_time_string_s(stats_selection.shortest_time.seconds, 0, (displayed_dive.dc.divemode == FREEDIVE)));
		} else {
			ui.timeLimits->setMaximum("");
			ui.timeLimits->setMinimum("");
		}
		ui.timeLimits->overrideMaxToolTipText(tr("Longest dive"));
		ui.timeLimits->overrideMinToolTipText(tr("Shortest dive"));
		ui.timeLimits->overrideAvgToolTipText(tr("Average length of all selected dives"));
		// now let's get some gas use statistics
		QVector<QPair<QString, int> > gasUsed;
		QString gasUsedString;
		volume_t vol;
		selectedDivesGasUsed(gasUsed);
		for (int j = 0; j < 20; j++) {
			if (gasUsed.isEmpty())
				break;
			QPair<QString, int> gasPair = gasUsed.last();
			gasUsed.pop_back();
			vol.mliter = gasPair.second;
			gasUsedString.append(gasPair.first).append(": ").append(get_volume_string(vol, true)).append("\n");
		}
		if (!gasUsed.isEmpty())
			gasUsedString.append("...");
		volume_t o2_tot = {}, he_tot = {};
		selected_dives_gas_parts(&o2_tot, &he_tot);

		/* No need to show the gas mixing information if diving
		 * with pure air, and only display the he / O2 part when
		 * it is used.
		 */
		if (he_tot.mliter || o2_tot.mliter) {
			gasUsedString.append(tr("These gases could be\nmixed from Air and using:\n"));
			if (he_tot.mliter)
				gasUsedString.append(QString("He: %1").arg(get_volume_string(he_tot, true)));
			if (he_tot.mliter && o2_tot.mliter)
				gasUsedString.append(tr(" and "));
			if (o2_tot.mliter)
				gasUsedString.append(QString("O2: %2\n").arg(get_volume_string(o2_tot, true)));
		}
		ui.gasConsumption->setText(gasUsedString);
		if(ui.locationTags->text().isEmpty())
			ui.locationTags->hide();
		else
			ui.locationTags->show();
		/* unset the special value text for date and time, just in case someone dove at midnight */
		ui.dateEdit->setSpecialValueText(QString(""));
		ui.timeEdit->setSpecialValueText(QString(""));

	} else {
		/* clear the fields */
		clearInfo();
		clearStats();
		clearEquipment();
		ui.rating->setCurrentStars(0);
		ui.visibility->setCurrentStars(0);
		ui.location->clear();
		/* set date and time to minimums which triggers showing the special value text */
		ui.dateEdit->setSpecialValueText(QString("-"));
		ui.dateEdit->setMinimumDate(QDate(1, 1, 1));
		ui.dateEdit->setDate(QDate(1, 1, 1));
		ui.timeEdit->setSpecialValueText(QString("-"));
		ui.timeEdit->setMinimumTime(QTime(0, 0, 0, 0));
		ui.timeEdit->setTime(QTime(0, 0, 0, 0));
	}
	editMode = rememberEM;
	ui.cylinders->view()->hideColumn(CylindersModel::DEPTH);
	if (get_dive_dc(&displayed_dive, dc_number)->divemode == CCR)
		ui.cylinders->view()->showColumn(CylindersModel::USE);
	else
		ui.cylinders->view()->hideColumn(CylindersModel::USE);

	if (verbose)
		qDebug() << "Set the current dive site:" << displayed_dive.dive_site_uuid;
	emit diveSiteChanged(get_dive_site_by_uuid(displayed_dive.dive_site_uuid));
}

void MainTab::addCylinder_clicked()
{
	if (editMode == NONE)
		enableEdition();
	cylindersModel->add();
}

void MainTab::addWeight_clicked()
{
	if (editMode == NONE)
		enableEdition();
	weightModel->add();
}

void MainTab::reload()
{
	suitModel.updateModel();
	buddyModel.updateModel();
	diveMasterModel.updateModel();
	tagModel.updateModel();
	LocationInformationModel::instance()->update();
}

// tricky little macro to edit all the selected dives
// loop over all dives, for each selected dive do WHAT, but do it
// last for the current dive; this is required in case the invocation
// wants to compare things to the original value in current_dive like it should
#define MODIFY_SELECTED_DIVES(WHAT)                            \
	do {                                                 \
		struct dive *mydive = NULL;                  \
		int _i;                                      \
		for_each_dive (_i, mydive) {                 \
			if (!mydive->selected || mydive == cd) \
				continue;                    \
							     \
			WHAT;                                \
		}                                            \
		mydive = cd;                                 \
		WHAT;                                        \
		mark_divelist_changed(true);                 \
	} while (0)

#define EDIT_TEXT(what)                                          \
	if (same_string(mydive->what, cd->what) || copyPaste) {  \
		free(mydive->what);                              \
		mydive->what = copy_string(displayed_dive.what); \
	}

MainTab::EditMode MainTab::getEditMode() const
{
	return editMode;
}

#define EDIT_VALUE(what)                             \
	if (mydive->what == cd->what || copyPaste) { \
		mydive->what = displayed_dive.what;  \
	}

void MainTab::refreshDisplayedDiveSite()
{
	if (displayed_dive_site.uuid) {
		copy_dive_site(get_dive_site_by_uuid(displayed_dive_site.uuid), &displayed_dive_site);
		ui.location->setCurrentDiveSiteUuid(displayed_dive_site.uuid);
	}
}

// when this is called we already have updated the current_dive and know that it exists
// there is no point in calling this function if there is no current dive
uint32_t MainTab::updateDiveSite(uint32_t pickedUuid, int divenr)
{
	struct dive *cd = get_dive(divenr);
	if (!cd)
		return 0;

	if (ui.location->text().isEmpty())
		return 0;

	if (pickedUuid == 0)
		return 0;

	const uint32_t origUuid = cd->dive_site_uuid;
	struct dive_site *origDs = get_dive_site_by_uuid(origUuid);
	struct dive_site *newDs = NULL;
	bool createdNewDive = false;

	if (pickedUuid == origUuid)
		return origUuid;

	if (pickedUuid == RECENTLY_ADDED_DIVESITE) {
		pickedUuid = create_dive_site(ui.location->text().isEmpty() ? qPrintable(tr("New dive site")) : qPrintable(ui.location->text()), displayed_dive.when);
		createdNewDive = true;
	}

	newDs = get_dive_site_by_uuid(pickedUuid);

	// Copy everything from the displayed_dive_site, so we have the latitude, longitude, notes, etc.
	// The user *might* be using wrongly the 'choose dive site' just to edit the name of it, sigh.
	if (origDs) {
		if(createdNewDive) {
			copy_dive_site(origDs, newDs);
			free(newDs->name);
			newDs->name = copy_string(qPrintable(ui.location->text().constData()));
			newDs->uuid = pickedUuid;
			qDebug() << "Creating and copying dive site";
		} else if (newDs->latitude.udeg == 0 && newDs->longitude.udeg == 0) {
			newDs->latitude.udeg = origDs->latitude.udeg;
			newDs->longitude.udeg = origDs->longitude.udeg;
			qDebug() << "Copying GPS information";
		}
	}

	if (origDs && pickedUuid != origDs->uuid && same_string(origDs->notes, "SubsurfaceWebservice")) {
		if (!is_dive_site_used(origDs->uuid, false)) {
			if (verbose)
				qDebug() << "delete the autogenerated dive site" << origDs->name;
			delete_dive_site(origDs->uuid);
		}
	}

	cd->dive_site_uuid = pickedUuid;
	qDebug() << "Setting the dive site id on the dive:" << pickedUuid;
	return pickedUuid;
}

void MainTab::acceptChanges()
{
	int i, addedId = -1;
	struct dive *d;
	bool do_replot = false;

	if(ui.location->hasFocus()) {
		this->setFocus();
	}

	acceptingEdit = true;
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment
	ui.dateEdit->setEnabled(true);
	hideMessage();
	ui.equipmentTab->setEnabled(true);
	if (editMode == ADD) {
		// We need to add the dive we just created to the dive list and select it.
		// Easy, right?
		struct dive *added_dive = clone_dive(&displayed_dive);
		record_dive(added_dive);
		addedId = added_dive->id;
		// make sure that the dive site is handled as well
		updateDiveSite(ui.location->currDiveSiteUuid(), get_idx_by_uniq_id(added_dive->id));

		// unselect everything as far as the UI is concerned and select the new
		// dive - we'll have to undo/redo this later after we resort the dive_table
		// but we need the dive selected for the middle part of this function - this
		// way we can reuse the code used for editing dives
		MainWindow::instance()->dive_list()->unselectDives();
		selected_dive = get_divenr(added_dive);
		amount_selected = 1;
	} else if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
		/* now figure out if things have changed */
		if (displayedTrip.notes && !same_string(displayedTrip.notes, currentTrip->notes)) {
			currentTrip->notes = copy_string(displayedTrip.notes);
			mark_divelist_changed(true);
		}
		if (displayedTrip.location && !same_string(displayedTrip.location, currentTrip->location)) {
			currentTrip->location = copy_string(displayedTrip.location);
			mark_divelist_changed(true);
		}
		currentTrip = NULL;
		ui.dateEdit->setEnabled(true);
	} else {
		if (editMode == MANUALLY_ADDED_DIVE) {
			// preserve any changes to the profile
			free(current_dive->dc.sample);
			copy_samples(&displayed_dive.dc, &current_dive->dc);
			addedId = displayed_dive.id;
		}
		struct dive *cd = current_dive;
		struct divecomputer *displayed_dc = get_dive_dc(&displayed_dive, dc_number);
		// now check if something has changed and if yes, edit the selected dives that
		// were identical with the master dive shown (and mark the divelist as changed)
		if (!same_string(displayed_dive.suit, cd->suit))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(suit));
		if (!same_string(displayed_dive.notes, cd->notes))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(notes));
		if (displayed_dive.rating != cd->rating)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(rating));
		if (displayed_dive.visibility != cd->visibility)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(visibility));
		if (displayed_dive.airtemp.mkelvin != cd->airtemp.mkelvin)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(airtemp.mkelvin));
		if (displayed_dc->divemode != current_dc->divemode) {
			MODIFY_SELECTED_DIVES(
				if (get_dive_dc(mydive, dc_number)->divemode == current_dc->divemode || copyPaste) {
					get_dive_dc(mydive, dc_number)->divemode = displayed_dc->divemode;
				}
			);
			MODIFY_SELECTED_DIVES(update_setpoint_events(mydive, get_dive_dc(mydive, dc_number)));
			do_replot = true;
		}
		if (displayed_dive.watertemp.mkelvin != cd->watertemp.mkelvin)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(watertemp.mkelvin));
		if (displayed_dive.when != cd->when) {
			time_t offset = cd->when - displayed_dive.when;
			MODIFY_SELECTED_DIVES(mydive->when -= offset;);
		}

		if (displayed_dive.dive_site_uuid != cd->dive_site_uuid)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(dive_site_uuid));

		// three text fields are somewhat special and are represented as tags
		// in the UI - they need somewhat smarter handling
		saveTaggedStrings();
		saveTags();

		if (editMode != ADD && cylindersModel->changed) {
			mark_divelist_changed(true);
			MODIFY_SELECTED_DIVES(
				for (int i = 0; i < MAX_CYLINDERS; i++) {
					if (mydive != cd) {
						if (same_string(mydive->cylinder[i].type.description, cd->cylinder[i].type.description) || copyPaste) {
							// if we started out with the same cylinder description (for multi-edit) or if we do copt & paste
							// make sure that we have the same cylinder type and copy the gasmix, but DON'T copy the start
							// and end pressures (those are per dive after all)
							if (!same_string(mydive->cylinder[i].type.description, displayed_dive.cylinder[i].type.description)) {
								free((void*)mydive->cylinder[i].type.description);
								mydive->cylinder[i].type.description = copy_string(displayed_dive.cylinder[i].type.description);
							}
							mydive->cylinder[i].type.size = displayed_dive.cylinder[i].type.size;
							mydive->cylinder[i].type.workingpressure = displayed_dive.cylinder[i].type.workingpressure;
							mydive->cylinder[i].gasmix = displayed_dive.cylinder[i].gasmix;
							mydive->cylinder[i].cylinder_use = displayed_dive.cylinder[i].cylinder_use;
							mydive->cylinder[i].depth = displayed_dive.cylinder[i].depth;
						}
					}
				}
			);
			for (int i = 0; i < MAX_CYLINDERS; i++) {
				// copy the cylinder but make sure we have our own copy of the strings
				free((void*)cd->cylinder[i].type.description);
				cd->cylinder[i] = displayed_dive.cylinder[i];
				cd->cylinder[i].type.description = copy_string(displayed_dive.cylinder[i].type.description);
			}
			/* if cylinders changed we may have changed gas change events
			 * - so far this is ONLY supported for a single selected dive */
			struct divecomputer *tdc = &current_dive->dc;
			struct divecomputer *sdc = &displayed_dive.dc;
			while(tdc && sdc) {
				free_events(tdc->events);
				copy_events(sdc, tdc);
				tdc = tdc->next;
				sdc = sdc->next;
			}
			do_replot = true;
		}

		if (weightModel->changed) {
			mark_divelist_changed(true);
			MODIFY_SELECTED_DIVES(
				for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
					if (mydive != cd && (copyPaste || same_string(mydive->weightsystem[i].description, cd->weightsystem[i].description))) {
						mydive->weightsystem[i] = displayed_dive.weightsystem[i];
						mydive->weightsystem[i].description = copy_string(displayed_dive.weightsystem[i].description);
					}
				}
			);
			for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
				cd->weightsystem[i] = displayed_dive.weightsystem[i];
				cd->weightsystem[i].description = copy_string(displayed_dive.weightsystem[i].description);
			}
		}

		// update the dive site for the selected dives that had the same dive site as the current dive
		uint32_t oldUuid = cd->dive_site_uuid;
		uint32_t newUuid = 0;
		MODIFY_SELECTED_DIVES(
			if (mydive->dive_site_uuid == current_dive->dive_site_uuid) {
				newUuid = updateDiveSite(newUuid == 0 ? ui.location->currDiveSiteUuid() : newUuid,  get_idx_by_uniq_id(mydive->id));
			}
		);
		if (!is_dive_site_used(oldUuid, false)) {
			if (verbose) {
				struct dive_site *ds = get_dive_site_by_uuid(oldUuid);
				qDebug() << "delete now unused dive site" << ((ds && ds->name) ? ds->name : "without name");
			}
			delete_dive_site(oldUuid);
			GlobeGPS::instance()->reload();
		}
		// the code above can change the correct uuid for the displayed dive site - and the
		// code below triggers an update of the display without re-initializing displayed_dive
		// so let's make sure here that our data is consistent now that we have handled the
		// dive sites
		displayed_dive.dive_site_uuid = current_dive->dive_site_uuid;
		struct dive_site *ds = get_dive_site_by_uuid(displayed_dive.dive_site_uuid);
		if (ds)
			copy_dive_site(ds, &displayed_dive_site);

		// each dive that was selected might have had the temperatures in its active divecomputer changed
		// so re-populate the temperatures - easiest way to do this is by calling fixup_dive
		for_each_dive (i, d) {
			if (d->selected) {
				fixup_dive(d);
				invalidate_dive_cache(d);
			}
		}
	}
	if (editMode != TRIP && current_dive->divetrip) {
		current_dive->divetrip->when = current_dive->when;
		find_new_trip_start_time(current_dive->divetrip);
	}
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE) {
		// we just added or edited the dive, let fixup_dive() make
		// sure we get the max depth right
		current_dive->maxdepth.mm = current_dc->maxdepth.mm = 0;
		fixup_dive(current_dive);
		set_dive_nr_for_current_dive();
		MainWindow::instance()->showProfile();
		mark_divelist_changed(true);
		DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::NOTHING);
	}
	int scrolledBy = MainWindow::instance()->dive_list()->verticalScrollBar()->sliderPosition();
	resetPallete();
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE) {
		// since a newly added dive could be in the middle of the dive_table we need
		// to resort the dive list and make sure the newly added dive gets selected again
		sort_table(&dive_table);
		MainWindow::instance()->dive_list()->reload(DiveTripModel::CURRENT, true);
		int newDiveNr = get_divenr(get_dive_by_uniq_id(addedId));
		MainWindow::instance()->dive_list()->unselectDives();
		MainWindow::instance()->dive_list()->selectDive(newDiveNr, true);
		editMode = NONE;
		MainWindow::instance()->refreshDisplay();
		MainWindow::instance()->graphics()->replot();
		emit addDiveFinished();
	} else {
		editMode = NONE;
		if (do_replot)
			MainWindow::instance()->graphics()->replot();
		MainWindow::instance()->dive_list()->rememberSelection();
		sort_table(&dive_table);
		MainWindow::instance()->refreshDisplay();
		MainWindow::instance()->dive_list()->restoreSelection();
	}
	DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::NOTHING);
	MainWindow::instance()->dive_list()->verticalScrollBar()->setSliderPosition(scrolledBy);
	MainWindow::instance()->dive_list()->setFocus();
	cylindersModel->changed = false;
	weightModel->changed = false;
	MainWindow::instance()->setEnabledToolbar(true);
	acceptingEdit = false;
	ui.editDiveSiteButton->setEnabled(true);
}

void MainTab::resetPallete()
{
	QPalette p;
	ui.buddy->setPalette(p);
	ui.notes->setPalette(p);
	ui.location->setPalette(p);
	ui.divemaster->setPalette(p);
	ui.suit->setPalette(p);
	ui.airtemp->setPalette(p);
	ui.DiveType->setPalette(p);
	ui.watertemp->setPalette(p);
	ui.dateEdit->setPalette(p);
	ui.timeEdit->setPalette(p);
	ui.tagWidget->setPalette(p);
	ui.diveTripLocation->setPalette(p);
	ui.duration->setPalette(p);
	ui.depth->setPalette(p);
}

#define EDIT_TEXT2(what, text)         \
	textByteArray = text.toUtf8(); \
	free(what);                    \
	what = strdup(textByteArray.data());

#define FREE_IF_DIFFERENT(what)              \
	if (displayed_dive.what != cd->what) \
		free(displayed_dive.what)

void MainTab::rejectChanges()
{
	EditMode lastMode = editMode;

	if (lastMode != NONE && current_dive &&
	    (modified ||
	     memcmp(&current_dive->cylinder[0], &displayed_dive.cylinder[0], sizeof(cylinder_t) * MAX_CYLINDERS) ||
	     memcmp(&current_dive->cylinder[0], &displayed_dive.weightsystem[0], sizeof(weightsystem_t) * MAX_WEIGHTSYSTEMS))) {
		if (QMessageBox::warning(MainWindow::instance(), TITLE_OR_TEXT(tr("Discard the changes?"),
									       tr("You are about to discard your changes.")),
					 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Discard) != QMessageBox::Discard) {
			return;
		}
	}
	ui.dateEdit->setEnabled(true);
	editMode = NONE;
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment
	hideMessage();
	resetPallete();
	// no harm done to call cancelPlan even if we were not in ADD or PLAN mode...
	DivePlannerPointsModel::instance()->cancelPlan();
	if(lastMode == ADD)
		MainWindow::instance()->dive_list()->restoreSelection();

	// now make sure that the correct dive is displayed
	if (selected_dive >= 0)
		copy_dive(current_dive, &displayed_dive);
	else
		clear_dive(&displayed_dive);
	updateDiveInfo(selected_dive < 0);
	DivePictureModel::instance()->updateDivePictures();
	// the user could have edited the location and then canceled the edit
	// let's get the correct location back in view
#ifndef NO_MARBLE
	GlobeGPS::instance()->centerOnDiveSite(get_dive_site_by_uuid(displayed_dive.dive_site_uuid));
#endif
	// show the profile and dive info
	MainWindow::instance()->graphics()->replot();
	MainWindow::instance()->setEnabledToolbar(true);
	cylindersModel->changed = false;
	weightModel->changed = false;
	cylindersModel->updateDive();
	weightModel->updateDive();
	extraDataModel->updateDive();
	ui.editDiveSiteButton->setEnabled(true);
}
#undef EDIT_TEXT2

void MainTab::markChangedWidget(QWidget *w)
{
	QPalette p;
	qreal h, s, l, a;
	enableEdition();
	qApp->palette().color(QPalette::Text).getHslF(&h, &s, &l, &a);
	p.setBrush(QPalette::Base, (l <= 0.3) ? QColor(Qt::yellow).lighter() : (l <= 0.6) ? QColor(Qt::yellow).light() : /* else */ QColor(Qt::yellow).darker(300));
	w->setPalette(p);
	modified = true;
}

void MainTab::on_buddy_textChanged()
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;

	if (same_string(displayed_dive.buddy, ui.buddy->toPlainText().toUtf8().data()))
		return;

	QStringList text_list = ui.buddy->toPlainText().split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString text = text_list.join(", ");
	free(displayed_dive.buddy);
	displayed_dive.buddy = strdup(text.toUtf8().data());
	markChangedWidget(ui.buddy);
}

void MainTab::on_divemaster_textChanged()
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;

	if (same_string(displayed_dive.divemaster, ui.divemaster->toPlainText().toUtf8().data()))
		return;

	QStringList text_list = ui.divemaster->toPlainText().split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString text = text_list.join(", ");
	free(displayed_dive.divemaster);
	displayed_dive.divemaster = strdup(text.toUtf8().data());
	markChangedWidget(ui.divemaster);
}

void MainTab::on_duration_textChanged(const QString &text)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	// parse this
	MainWindow::instance()->graphics()->setReplot(false);
	if (!isEditing())
		enableEdition();
	displayed_dive.dc.duration.seconds = parseDurationToSeconds(text);
	displayed_dive.duration = displayed_dive.dc.duration;
	displayed_dive.dc.meandepth.mm = 0;
	displayed_dive.dc.samples = 0;
	DivePlannerPointsModel::instance()->loadFromDive(&displayed_dive);
	markChangedWidget(ui.duration);
	MainWindow::instance()->graphics()->setReplot(true);
	MainWindow::instance()->graphics()->plotDive();

}

void MainTab::on_depth_textChanged(const QString &text)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	// don't replot until we set things up the way we want them
	MainWindow::instance()->graphics()->setReplot(false);
	if (!isEditing())
		enableEdition();
	displayed_dive.dc.maxdepth.mm = parseLengthToMm(text);
	displayed_dive.maxdepth = displayed_dive.dc.maxdepth;
	displayed_dive.dc.meandepth.mm = 0;
	displayed_dive.dc.samples = 0;
	DivePlannerPointsModel::instance()->loadFromDive(&displayed_dive);
	markChangedWidget(ui.depth);
	MainWindow::instance()->graphics()->setReplot(true);
	MainWindow::instance()->graphics()->plotDive();
}

void MainTab::on_airtemp_textChanged(const QString &text)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	displayed_dive.airtemp.mkelvin = parseTemperatureToMkelvin(text);
	markChangedWidget(ui.airtemp);
	validate_temp_field(ui.airtemp, text);
}

void MainTab::divetype_Changed(int index)
{
	if (editMode == IGNORE)
		return;
	struct divecomputer *displayed_dc = get_dive_dc(&displayed_dive, dc_number);
	displayed_dc->divemode = (enum dive_comp_type) index;
	update_setpoint_events(&displayed_dive, displayed_dc);
	markChangedWidget(ui.DiveType);
	MainWindow::instance()->graphics()->recalcCeiling();
}

void MainTab::on_watertemp_textChanged(const QString &text)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	displayed_dive.watertemp.mkelvin = parseTemperatureToMkelvin(text);
	markChangedWidget(ui.watertemp);
	validate_temp_field(ui.watertemp, text);
}

void MainTab::validate_temp_field(QLineEdit *tempField, const QString &text)
{
	static bool missing_unit = false;
	static bool missing_precision = false;
	if (!text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.][0-9]+){0,1}(°[CF]){0,1}$")) &&
	    !text.isEmpty() &&
	    !text.contains(QRegExp("^[-+]$"))) {
		if (text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.][0-9]+){0,1}(°)$")) && !missing_unit) {
			if (!missing_unit) {
				missing_unit = true;
				return;
			}
		}
		if (text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.]){0,1}(°[CF]){0,1}$")) && !missing_precision) {
			if (!missing_precision) {
				missing_precision = true;
				return;
			}
		}
		QPalette p;
		p.setBrush(QPalette::Base, QColor(Qt::red).lighter());
		tempField->setPalette(p);
	} else {
		missing_unit = false;
		missing_precision = false;
	}
}

void MainTab::on_dateEdit_dateChanged(const QDate &date)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	markChangedWidget(ui.dateEdit);
	QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(1000*displayed_dive.when, Qt::UTC);
	dateTime.setTimeSpec(Qt::UTC);
	dateTime.setDate(date);
	DivePlannerPointsModel::instance()->getDiveplan().when = displayed_dive.when = dateTime.toTime_t();
	emit dateTimeChanged();
}

void MainTab::on_timeEdit_timeChanged(const QTime &time)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	markChangedWidget(ui.timeEdit);
	QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(1000*displayed_dive.when, Qt::UTC);
	dateTime.setTimeSpec(Qt::UTC);
	dateTime.setTime(time);
	DivePlannerPointsModel::instance()->getDiveplan().when = displayed_dive.when = dateTime.toTime_t();
	emit dateTimeChanged();
}

// changing the tags on multiple dives is semantically strange - what's the right thing to do?
// here's what I think... add the tags that were added to the displayed dive and remove the tags
// that were removed from it
void MainTab::saveTags()
{
	struct dive *cd = current_dive;
	struct tag_entry *added_list = NULL;
	struct tag_entry *removed_list = NULL;
	struct tag_entry *tl;

	taglist_free(displayed_dive.tag_list);
	displayed_dive.tag_list = NULL;
	Q_FOREACH (const QString& tag, ui.tagWidget->getBlockStringList())
		taglist_add_tag(&displayed_dive.tag_list, tag.toUtf8().data());
	taglist_cleanup(&displayed_dive.tag_list);

	// figure out which tags were added and which tags were removed
	added_list = taglist_added(cd->tag_list, displayed_dive.tag_list);
	removed_list = taglist_added(displayed_dive.tag_list, cd->tag_list);
	// dump_taglist("added tags:", added_list);
	// dump_taglist("removed tags:", removed_list);

	// we need to check if the tags were changed before just overwriting them
	if (added_list == NULL && removed_list == NULL)
		return;

	MODIFY_SELECTED_DIVES(
		// create a new tag list and all the existing tags that were not
		// removed and then all the added tags
		struct tag_entry *new_tag_list;
		new_tag_list = NULL;
		tl = mydive->tag_list;
		while (tl) {
			if (!taglist_contains(removed_list, tl->tag->name))
				taglist_add_tag(&new_tag_list, tl->tag->name);
			tl = tl->next;
		}
		tl = added_list;
		while (tl) {
			taglist_add_tag(&new_tag_list, tl->tag->name);
			tl = tl->next;
		}
		taglist_free(mydive->tag_list);
		mydive->tag_list = new_tag_list;
	);
	taglist_free(added_list);
	taglist_free(removed_list);
}

// buddy and divemaster are represented in the UI just like the tags, but the internal
// representation is just a string (with commas as delimiters). So we need to do the same
// thing we did for tags, just differently
void MainTab::saveTaggedStrings()
{
	QStringList addedList, removedList;
	struct dive *cd = current_dive;

	diffTaggedStrings(cd->buddy, displayed_dive.buddy, addedList, removedList);
	MODIFY_SELECTED_DIVES(
		QStringList oldList = QString(mydive->buddy).split(QRegExp("\\s*,\\s*"), QString::SkipEmptyParts);
		QString newString;
		QString comma;
		Q_FOREACH (const QString tag, oldList) {
			if (!removedList.contains(tag, Qt::CaseInsensitive)) {
				newString += comma + tag;
				comma = ", ";
			}
		}
		Q_FOREACH (const QString tag, addedList) {
			if (!oldList.contains(tag, Qt::CaseInsensitive)) {
				newString += comma + tag;
				comma = ", ";
			}
		}
		free(mydive->buddy);
		mydive->buddy = copy_string(qPrintable(newString));
	);
	addedList.clear();
	removedList.clear();
	diffTaggedStrings(cd->divemaster, displayed_dive.divemaster, addedList, removedList);
	MODIFY_SELECTED_DIVES(
		QStringList oldList = QString(mydive->divemaster).split(QRegExp("\\s*,\\s*"), QString::SkipEmptyParts);
		QString newString;
		QString comma;
		Q_FOREACH (const QString tag, oldList) {
			if (!removedList.contains(tag, Qt::CaseInsensitive)) {
				newString += comma + tag;
				comma = ", ";
			}
		}
		Q_FOREACH (const QString tag, addedList) {
			if (!oldList.contains(tag, Qt::CaseInsensitive)) {
				newString += comma + tag;
				comma = ", ";
			}
		}
		free(mydive->divemaster);
		mydive->divemaster = copy_string(qPrintable(newString));
	);
}

void MainTab::diffTaggedStrings(QString currentString, QString displayedString, QStringList &addedList, QStringList &removedList)
{
	QStringList displayedList, currentList;
	currentList = currentString.split(',', QString::SkipEmptyParts);
	displayedList = displayedString.split(',', QString::SkipEmptyParts);
	Q_FOREACH ( const QString tag, currentList) {
		if (!displayedList.contains(tag, Qt::CaseInsensitive))
			removedList << tag.trimmed();
	}
	Q_FOREACH (const QString tag, displayedList) {
		if (!currentList.contains(tag, Qt::CaseInsensitive))
			addedList << tag.trimmed();
	}
}

void MainTab::on_tagWidget_textChanged()
{
	char buf[1024];

	if (editMode == IGNORE || acceptingEdit == true)
		return;

	taglist_get_tagstring(displayed_dive.tag_list, buf, 1024);
	if (same_string(buf, ui.tagWidget->toPlainText().toUtf8().data()))
		return;

	markChangedWidget(ui.tagWidget);
}

void MainTab::on_location_textChanged()
{
	if (editMode == IGNORE)
		return;

	// we don't want to act on the edit until editing is finished,
	// but we want to mark the field so it's obvious it is being edited
	QString currentLocation;
	struct dive_site *ds = get_dive_site_by_uuid(displayed_dive.dive_site_uuid);
	if (ds)
		currentLocation = ds->name;
	if (ui.location->text() != currentLocation)
		markChangedWidget(ui.location);
}

void MainTab::on_location_diveSiteSelected()
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;

	if (ui.location->text().isEmpty()) {
		displayed_dive.dive_site_uuid = 0;
		markChangedWidget(ui.location);
		emit diveSiteChanged(0);
		return;
	} else {
		if (ui.location->currDiveSiteUuid() != displayed_dive.dive_site_uuid) {
			markChangedWidget(ui.location);
		} else {
			QPalette p;
			ui.location->setPalette(p);
		}
	}
}

void MainTab::on_diveTripLocation_textEdited(const QString& text)
{
	if (currentTrip) {
		free(displayedTrip.location);
		displayedTrip.location = strdup(qPrintable(text));
		markChangedWidget(ui.diveTripLocation);
	}
}

void MainTab::on_suit_textChanged(const QString &text)
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	free(displayed_dive.suit);
	displayed_dive.suit = strdup(text.toUtf8().data());
	markChangedWidget(ui.suit);
}

void MainTab::on_notes_textChanged()
{
	if (editMode == IGNORE || acceptingEdit == true)
		return;
	if (currentTrip) {
		if (same_string(displayedTrip.notes, ui.notes->toPlainText().toUtf8().data()))
			return;
		free(displayedTrip.notes);
		displayedTrip.notes = strdup(ui.notes->toPlainText().toUtf8().data());
	} else {
		if (same_string(displayed_dive.notes, ui.notes->toPlainText().toUtf8().data()))
			return;
		free(displayed_dive.notes);
		if (ui.notes->toHtml().indexOf("<table") != -1)
			displayed_dive.notes = strdup(ui.notes->toHtml().toUtf8().data());
		else
			displayed_dive.notes = strdup(ui.notes->toPlainText().toUtf8().data());
	}
	markChangedWidget(ui.notes);
}

void MainTab::on_rating_valueChanged(int value)
{
	if (acceptingEdit == true)
		return;
	if (displayed_dive.rating != value) {
		displayed_dive.rating = value;
		modified = true;
		enableEdition();
	}
}

void MainTab::on_visibility_valueChanged(int value)
{
	if (acceptingEdit == true)
		return;
	if (displayed_dive.visibility != value) {
		displayed_dive.visibility = value;
		modified = true;
		enableEdition();
	}
}

#undef MODIFY_SELECTED_DIVES
#undef EDIT_TEXT
#undef EDIT_VALUE

void MainTab::editCylinderWidget(const QModelIndex &index)
{
	// we need a local copy or bad things happen when enableEdition() is called
	QModelIndex editIndex = index;
	if (cylindersModel->changed && editMode == NONE) {
		enableEdition();
		return;
	}
	if (editIndex.isValid() && editIndex.column() != CylindersModel::REMOVE) {
		if (editMode == NONE)
			enableEdition();
		ui.cylinders->edit(editIndex);
	}
}

void MainTab::editWeightWidget(const QModelIndex &index)
{
	if (editMode == NONE)
		enableEdition();

	if (index.isValid() && index.column() != WeightModel::REMOVE)
		ui.weights->edit(index);
}

void MainTab::escDetected()
{
	if (editMode != NONE)
		rejectChanges();
}

void MainTab::photoDoubleClicked(const QString filePath)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void MainTab::removeSelectedPhotos()
{
	bool last = false;
	if (!ui.photosView->selectionModel()->hasSelection())
		return;
	QModelIndexList indexes =  ui.photosView->selectionModel()->selectedRows();
	if (indexes.count() == 0)
		indexes = ui.photosView->selectionModel()->selectedIndexes();
	QModelIndex photo = indexes.first();
	do {
		photo = indexes.first();
		last = indexes.count() == 1;
		if (photo.isValid()) {
			QString fileUrl = photo.data(Qt::DisplayPropertyRole).toString();
			if (fileUrl.length() > 0)
				DivePictureModel::instance()->removePicture(fileUrl, last);
		}
		indexes.removeFirst();
	} while(!indexes.isEmpty());
}

void MainTab::removeAllPhotos()
{
	if (QMessageBox::warning(this, tr("Deleting Images"), tr("Are you sure you want to delete all images?"), QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel) != QMessageBox::Cancel ) {
		ui.photosView->selectAll();
		removeSelectedPhotos();
	}
}

void MainTab::addPhotosFromFile()
{
	MainWindow::instance()->dive_list()->loadImages();
}

void MainTab::addPhotosFromURL()
{
	MainWindow::instance()->dive_list()->loadWebImages();
}

#define SHOW_SELECTIVE(_component) \
	if (what._component)       \
		ui._component->setText(displayed_dive._component);

void MainTab::showAndTriggerEditSelective(struct dive_components what)
{
	// take the data in our copyPasteDive and apply it to selected dives
	enableEdition();
	copyPaste = true;
	SHOW_SELECTIVE(buddy);
	SHOW_SELECTIVE(divemaster);
	SHOW_SELECTIVE(suit);
	if (what.notes) {
		QString tmp(displayed_dive.notes);
		if (tmp.contains("<table"))
			ui.notes->setHtml(tmp);
		else
			ui.notes->setPlainText(tmp);
	}
	if (what.rating)
		ui.rating->setCurrentStars(displayed_dive.rating);
	if (what.visibility)
		ui.visibility->setCurrentStars(displayed_dive.visibility);
	if (what.divesite)
		ui.location->setCurrentDiveSiteUuid(displayed_dive.dive_site_uuid);
	if (what.tags) {
		char buf[1024];
		taglist_get_tagstring(displayed_dive.tag_list, buf, 1024);
		ui.tagWidget->setText(QString(buf));
	}
	if (what.cylinders) {
		cylindersModel->updateDive();
		cylindersModel->changed = true;
	}
	if (what.weights) {
		weightModel->updateDive();
		weightModel->changed = true;
	}
}

void MainTab::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu popup(this);
	popup.addAction(tr("Load image(s) from file(s)"), this, SLOT(addPhotosFromFile()));
	popup.addAction(tr("Load image(s) from web"), this, SLOT(addPhotosFromURL()));
	popup.addSeparator();
	popup.addAction(tr("Delete selected images"), this, SLOT(removeSelectedPhotos()));
	popup.addAction(tr("Delete all images"), this, SLOT(removeAllPhotos()));
	popup.exec(event->globalPos());
	event->accept();
}
