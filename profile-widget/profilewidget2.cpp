#include "profile-widget/profilewidget2.h"
#include "qt-models/diveplotdatamodel.h"
#include "core/helpers.h"
#include "core/profile.h"
#include "profile-widget/diveeventitem.h"
#include "profile-widget/divetextitem.h"
#include "profile-widget/divetooltipitem.h"
#include "core/planner.h"
#include "core/device.h"
#include "profile-widget/ruleritem.h"
#include "profile-widget/tankitem.h"
#include "core/pref.h"
#include "qt-models/diveplannermodel.h"
#include "qt-models/models.h"
#include "qt-models/divepicturemodel.h"
#include "core/divelist.h"
#include "core/subsurface-qt/SettingsObjectWrapper.h"
#ifndef SUBSURFACE_MOBILE
#include "desktop-widgets/diveplanner.h"
#include "desktop-widgets/simplewidgets.h"
#include "desktop-widgets/divepicturewidget.h"
#include "core/qthelper.h"
#endif

#include <libdivecomputer/parser.h>
#include <QScrollBar>
#include <QtCore/qmath.h>
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>
#include <QWheelEvent>
#include <QSettings>
#include <QMenu>

#ifndef QT_NO_DEBUG
#include <QTableView>
#endif
#ifndef SUBSURFACE_MOBILE
#include "desktop-widgets/preferences/preferencesdialog.h"
#endif
#include <QtWidgets>

// a couple of helpers we need
extern bool haveFilesOnCommandLine();

/* This is the global 'Item position' variable.
 * it should tell you where to position things up
 * on the canvas.
 *
 * please, please, please, use this instead of
 * hard coding the item on the scene with a random
 * value.
 */
static struct _ItemPos {
	struct _Pos {
		QPointF on;
		QPointF off;
	};
	struct _Axis {
		_Pos pos;
		QLineF shrinked;
		QLineF expanded;
		QLineF intermediate;
	};
	_Pos background;
	_Pos dcLabel;
	_Pos tankBar;
	_Axis depth;
	_Axis partialPressure;
	_Axis partialPressureTissue;
	_Axis partialPressureWithTankBar;
	_Axis percentage;
	_Axis percentageWithTankBar;
	_Axis time;
	_Axis cylinder;
	_Axis temperature;
	_Axis temperatureAll;
	_Axis heartBeat;
	_Axis heartBeatWithTankBar;
} itemPos;

ProfileWidget2::ProfileWidget2(QWidget *parent) : QGraphicsView(parent),
	currentState(INVALID),
	dataModel(new DivePlotDataModel(this)),
	zoomLevel(0),
	zoomFactor(1.15),
	background(new DivePixmapItem()),
	backgroundFile(":poster"),
#ifndef SUBSURFACE_MOBILE
	toolTipItem(new ToolTipItem()),
#endif
	isPlotZoomed(prefs.zoomed_plot),// no! bad use of prefs. 'PreferencesDialog::loadSettings' NOT CALLED yet.
	profileYAxis(new DepthAxis(this)),
	gasYAxis(new PartialGasPressureAxis(this)),
	temperatureAxis(new TemperatureAxis(this)),
	timeAxis(new TimeAxis(this)),
	diveProfileItem(new DiveProfileItem()),
	temperatureItem(new DiveTemperatureItem()),
	meanDepthItem(new DiveMeanDepthItem()),
	cylinderPressureAxis(new DiveCartesianAxis(this)),
	gasPressureItem(new DiveGasPressureItem()),
	diveComputerText(new DiveTextItem()),
	reportedCeiling(new DiveReportedCeiling()),
#ifndef SUBSURFACE_MOBILE
	diveCeiling(new DiveCalculatedCeiling(this)),
	decoModelParameters(new DiveTextItem()),
	pn2GasItem(new PartialPressureGasItem()),
	pheGasItem(new PartialPressureGasItem()),
	po2GasItem(new PartialPressureGasItem()),
	o2SetpointGasItem(new PartialPressureGasItem()),
	ccrsensor1GasItem(new PartialPressureGasItem()),
	ccrsensor2GasItem(new PartialPressureGasItem()),
	ccrsensor3GasItem(new PartialPressureGasItem()),
	heartBeatAxis(new DiveCartesianAxis(this)),
	heartBeatItem(new DiveHeartrateItem()),
	percentageAxis(new DiveCartesianAxis(this)),
	ambPressureItem(new DiveAmbPressureItem()),
	gflineItem(new DiveGFLineItem()),
	mouseFollowerVertical(new DiveLineItem()),
	mouseFollowerHorizontal(new DiveLineItem()),
	rulerItem(new RulerItem2()),
#endif
	tankItem(new TankItem()),
	isGrayscale(false),
	printMode(false),
	shouldCalculateMaxTime(true),
	shouldCalculateMaxDepth(true),
	fontPrintScale(1.0)
{
	// would like to be able to ASSERT here that PreferencesDialog::loadSettings has been called.
	isPlotZoomed = prefs.zoomed_plot; // now it seems that 'prefs' has loaded our preferences

	memset(&plotInfo, 0, sizeof(plotInfo));

	setupSceneAndFlags();
	setupItemSizes();
	setupItemOnScene();
	addItemsToScene();
	scene()->installEventFilter(this);
#ifndef SUBSURFACE_MOBILE
	QAction *action = NULL;
	setAcceptDrops(true);

#define ADD_ACTION(SHORTCUT, Slot)                                  \
	action = new QAction(this);                                 \
	action->setShortcut(SHORTCUT);                              \
	action->setShortcutContext(Qt::WindowShortcut);             \
	addAction(action);                                          \
	connect(action, SIGNAL(triggered(bool)), this, SLOT(Slot)); \
	actionsForKeys[SHORTCUT] = action;

	ADD_ACTION(Qt::Key_Escape, keyEscAction());
	ADD_ACTION(Qt::Key_Delete, keyDeleteAction());
	ADD_ACTION(Qt::Key_Up, keyUpAction());
	ADD_ACTION(Qt::Key_Down, keyDownAction());
	ADD_ACTION(Qt::Key_Left, keyLeftAction());
	ADD_ACTION(Qt::Key_Right, keyRightAction());
#undef ADD_ACTION
#endif // SUBSURFACE_MOBILE

#if !defined(QT_NO_DEBUG) && defined(SHOW_PLOT_INFO_TABLE)
	QTableView *diveDepthTableView = new QTableView();
	diveDepthTableView->setModel(dataModel);
	diveDepthTableView->show();
#endif
}


ProfileWidget2::~ProfileWidget2()
{
	delete background;
	delete profileYAxis;
	delete gasYAxis;
	delete temperatureAxis;
	delete timeAxis;
	delete diveProfileItem;
	delete temperatureItem;
	delete meanDepthItem;
	delete cylinderPressureAxis;
	delete gasPressureItem;
	delete diveComputerText;
	delete reportedCeiling;
#ifndef SUBSURFACE_MOBILE
	delete toolTipItem;
	delete diveCeiling;
	delete pn2GasItem;
	delete pheGasItem;
	delete po2GasItem;
	delete o2SetpointGasItem;
	delete ccrsensor1GasItem;
	delete ccrsensor2GasItem;
	delete ccrsensor3GasItem;
	delete heartBeatAxis;
	delete heartBeatItem;
	delete percentageAxis;
	delete ambPressureItem;
	delete gflineItem;
	delete mouseFollowerVertical;
	delete mouseFollowerHorizontal;
	delete rulerItem;
#endif
	delete tankItem;
}

#define SUBSURFACE_OBJ_DATA 1
#define SUBSURFACE_OBJ_DC_TEXT 0x42

void ProfileWidget2::addItemsToScene()
{
	scene()->addItem(background);
	scene()->addItem(profileYAxis);
	scene()->addItem(gasYAxis);
	scene()->addItem(temperatureAxis);
	scene()->addItem(timeAxis);
	scene()->addItem(diveProfileItem);
	scene()->addItem(cylinderPressureAxis);
	scene()->addItem(temperatureItem);
	scene()->addItem(meanDepthItem);
	scene()->addItem(gasPressureItem);
	// I cannot seem to figure out if an object that I find with itemAt() on the scene
	// is the object I am looking for - my guess is there's a simple way in Qt to do that
	// but nothing I tried worked.
	// so instead this adds a special magic key/value pair to the object to mark it
	diveComputerText->setData(SUBSURFACE_OBJ_DATA, SUBSURFACE_OBJ_DC_TEXT);
	scene()->addItem(diveComputerText);
	scene()->addItem(reportedCeiling);
#ifndef SUBSURFACE_MOBILE
	scene()->addItem(toolTipItem);
	scene()->addItem(diveCeiling);
	scene()->addItem(decoModelParameters);
	scene()->addItem(pn2GasItem);
	scene()->addItem(pheGasItem);
	scene()->addItem(po2GasItem);
	scene()->addItem(o2SetpointGasItem);
	scene()->addItem(ccrsensor1GasItem);
	scene()->addItem(ccrsensor2GasItem);
	scene()->addItem(ccrsensor3GasItem);
	scene()->addItem(percentageAxis);
	scene()->addItem(heartBeatAxis);
	scene()->addItem(heartBeatItem);
	scene()->addItem(rulerItem);
	scene()->addItem(rulerItem->sourceNode());
	scene()->addItem(rulerItem->destNode());
	scene()->addItem(tankItem);
	scene()->addItem(mouseFollowerHorizontal);
	scene()->addItem(mouseFollowerVertical);
	QPen pen(QColor(Qt::red).lighter());
	pen.setWidth(0);
	mouseFollowerHorizontal->setPen(pen);
	mouseFollowerVertical->setPen(pen);

	Q_FOREACH (DiveCalculatedTissue *tissue, allTissues) {
		scene()->addItem(tissue);
	}
	Q_FOREACH (DivePercentageItem *percentage, allPercentages) {
		scene()->addItem(percentage);
	}
	scene()->addItem(ambPressureItem);
	scene()->addItem(gflineItem);
#endif
}

void ProfileWidget2::setupItemOnScene()
{
	background->setZValue(9999);
#ifndef SUBSURFACE_MOBILE
	toolTipItem->setZValue(9998);
	toolTipItem->setTimeAxis(timeAxis);
	rulerItem->setZValue(9997);
#endif
	tankItem->setZValue(100);

	profileYAxis->setOrientation(DiveCartesianAxis::TopToBottom);
	profileYAxis->setMinimum(0);
	profileYAxis->setTickInterval(M_OR_FT(10, 30));
	profileYAxis->setTickSize(0.5);
	profileYAxis->setLineSize(96);

	timeAxis->setLineSize(92);
	timeAxis->setTickSize(-0.5);

	gasYAxis->setOrientation(DiveCartesianAxis::BottomToTop);
	gasYAxis->setTickInterval(1);
	gasYAxis->setTickSize(1);
	gasYAxis->setMinimum(0);
	gasYAxis->setModel(dataModel);
	gasYAxis->setFontLabelScale(0.7);
	gasYAxis->setLineSize(96);

#ifndef SUBSURFACE_MOBILE
	heartBeatAxis->setOrientation(DiveCartesianAxis::BottomToTop);
	heartBeatAxis->setTickSize(0.2);
	heartBeatAxis->setTickInterval(10);
	heartBeatAxis->setFontLabelScale(0.7);
	heartBeatAxis->setLineSize(96);

	percentageAxis->setOrientation(DiveCartesianAxis::BottomToTop);
	percentageAxis->setTickSize(0.2);
	percentageAxis->setTickInterval(10);
	percentageAxis->setFontLabelScale(0.7);
	percentageAxis->setLineSize(96);
#endif

	temperatureAxis->setOrientation(DiveCartesianAxis::BottomToTop);
	temperatureAxis->setTickSize(2);
	temperatureAxis->setTickInterval(300);

	cylinderPressureAxis->setOrientation(DiveCartesianAxis::BottomToTop);
	cylinderPressureAxis->setTickSize(2);
	cylinderPressureAxis->setTickInterval(30000);


	diveComputerText->setAlignment(Qt::AlignRight | Qt::AlignTop);
	diveComputerText->setBrush(getColor(TIME_TEXT, isGrayscale));

	tankItem->setHorizontalAxis(timeAxis);

#ifndef SUBSURFACE_MOBILE
	rulerItem->setAxis(timeAxis, profileYAxis);

	// show the deco model parameters at the top in the center
	decoModelParameters->setY(0);
	decoModelParameters->setX(50);
	decoModelParameters->setBrush(getColor(PRESSURE_TEXT));
	decoModelParameters->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
	setupItem(diveCeiling, timeAxis, profileYAxis, dataModel, DivePlotDataModel::CEILING, DivePlotDataModel::TIME, 1);
	for (int i = 0; i < 16; i++) {
		DiveCalculatedTissue *tissueItem = new DiveCalculatedTissue(this);
		setupItem(tissueItem, timeAxis, profileYAxis, dataModel, DivePlotDataModel::TISSUE_1 + i, DivePlotDataModel::TIME, 1 + i);
		allTissues.append(tissueItem);
		DivePercentageItem *percentageItem = new DivePercentageItem(i);
		setupItem(percentageItem, timeAxis, percentageAxis, dataModel, DivePlotDataModel::PERCENTAGE_1 + i, DivePlotDataModel::TIME, 1 + i);
		allPercentages.append(percentageItem);
	}
	setupItem(heartBeatItem, timeAxis, heartBeatAxis, dataModel, DivePlotDataModel::HEARTBEAT, DivePlotDataModel::TIME, 1);
	setupItem(ambPressureItem, timeAxis, percentageAxis, dataModel, DivePlotDataModel::AMBPRESSURE, DivePlotDataModel::TIME, 1);
	setupItem(gflineItem, timeAxis, percentageAxis, dataModel, DivePlotDataModel::GFLINE, DivePlotDataModel::TIME, 1);
#endif
	setupItem(reportedCeiling, timeAxis, profileYAxis, dataModel, DivePlotDataModel::CEILING, DivePlotDataModel::TIME, 1);
	setupItem(gasPressureItem, timeAxis, cylinderPressureAxis, dataModel, DivePlotDataModel::TEMPERATURE, DivePlotDataModel::TIME, 1);
	setupItem(temperatureItem, timeAxis, temperatureAxis, dataModel, DivePlotDataModel::TEMPERATURE, DivePlotDataModel::TIME, 1);
	setupItem(diveProfileItem, timeAxis, profileYAxis, dataModel, DivePlotDataModel::DEPTH, DivePlotDataModel::TIME, 0);
	setupItem(meanDepthItem, timeAxis, profileYAxis, dataModel, DivePlotDataModel::INSTANT_MEANDEPTH, DivePlotDataModel::TIME, 1);

#ifndef SUBSURFACE_MOBILE
#define CREATE_PP_GAS(ITEM, VERTICAL_COLUMN, COLOR, COLOR_ALERT, THRESHOULD_SETTINGS)              \
	setupItem(ITEM, timeAxis, gasYAxis, dataModel, DivePlotDataModel::VERTICAL_COLUMN, DivePlotDataModel::TIME, 0); \
	ITEM->setThreshouldSettingsKey(THRESHOULD_SETTINGS);                                                            \
	ITEM->setColors(getColor(COLOR, isGrayscale), getColor(COLOR_ALERT, isGrayscale));                              \
	ITEM->settingsChanged();                                                                                        \
	ITEM->setZValue(99);

	CREATE_PP_GAS(pn2GasItem, PN2, PN2, PN2_ALERT, &prefs.pp_graphs.pn2_threshold);
	CREATE_PP_GAS(pheGasItem, PHE, PHE, PHE_ALERT, &prefs.pp_graphs.phe_threshold);
	CREATE_PP_GAS(po2GasItem, PO2, PO2, PO2_ALERT, &prefs.pp_graphs.po2_threshold);
	CREATE_PP_GAS(o2SetpointGasItem, O2SETPOINT, PO2_ALERT, PO2_ALERT, &prefs.pp_graphs.po2_threshold);
	CREATE_PP_GAS(ccrsensor1GasItem, CCRSENSOR1, CCRSENSOR1, PO2_ALERT, &prefs.pp_graphs.po2_threshold);
	CREATE_PP_GAS(ccrsensor2GasItem, CCRSENSOR2, CCRSENSOR2, PO2_ALERT, &prefs.pp_graphs.po2_threshold);
	CREATE_PP_GAS(ccrsensor3GasItem, CCRSENSOR3, CCRSENSOR3, PO2_ALERT, &prefs.pp_graphs.po2_threshold);

	// Visibility Connections
	connect(SettingsObjectWrapper::instance()->pp_gas, &PartialPressureGasSettings::showPheChanged, pheGasItem, &PartialPressureGasItem::setVisible);
	connect(SettingsObjectWrapper::instance()->pp_gas, &PartialPressureGasSettings::showPo2Changed, po2GasItem, &PartialPressureGasItem::setVisible);
	connect(SettingsObjectWrapper::instance()->pp_gas, &PartialPressureGasSettings::showPn2Changed, pn2GasItem, &PartialPressureGasItem::setVisible);
	connect(SettingsObjectWrapper::instance()->pp_gas, &PartialPressureGasSettings::showPo2Changed, o2SetpointGasItem, &PartialPressureGasItem::setVisible);

	//WARNING: The old code was broken, I'm not sure what should trigger the visibility of those graphs, since the old code didn't triggered them
	// because it was using a wrong settings.
	connect(SettingsObjectWrapper::instance()->techDetails, &TechnicalDetailsSettings::showCCRSensorsChanged, ccrsensor1GasItem, &PartialPressureGasItem::setVisible);
	connect(SettingsObjectWrapper::instance()->techDetails, &TechnicalDetailsSettings::showCCRSensorsChanged, ccrsensor2GasItem, &PartialPressureGasItem::setVisible);
	connect(SettingsObjectWrapper::instance()->techDetails, &TechnicalDetailsSettings::showCCRSensorsChanged, ccrsensor3GasItem, &PartialPressureGasItem::setVisible);
#undef CREATE_PP_GAS

	heartBeatAxis->setTextVisible(true);
	heartBeatAxis->setLinesVisible(true);
	percentageAxis->setTextVisible(true);
	percentageAxis->setLinesVisible(true);
#endif
	temperatureAxis->setTextVisible(false);
	temperatureAxis->setLinesVisible(false);
	cylinderPressureAxis->setTextVisible(false);
	cylinderPressureAxis->setLinesVisible(false);
	timeAxis->setLinesVisible(true);
	profileYAxis->setLinesVisible(true);
	gasYAxis->setZValue(timeAxis->zValue() + 1);

	replotEnabled = true;
}

void ProfileWidget2::replot(struct dive *d)
{
	if (!replotEnabled)
		return;
	dataModel->clear();
	plotDive(d, true);
}

void ProfileWidget2::setupItemSizes()
{
	// Scene is *always* (double) 100 / 100.
	// Background Config
	/* Much probably a better math is needed here.
	 * good thing is that we only need to change the
	 * Axis and everything else is auto-adjusted.*
	 */

	itemPos.background.on.setX(0);
	itemPos.background.on.setY(0);
	itemPos.background.off.setX(0);
	itemPos.background.off.setY(110);

	//Depth Axis Config
	itemPos.depth.pos.on.setX(3);
	itemPos.depth.pos.on.setY(3);
	itemPos.depth.pos.off.setX(-2);
	itemPos.depth.pos.off.setY(3);
	itemPos.depth.expanded.setP1(QPointF(0, 0));
	itemPos.depth.expanded.setP2(QPointF(0, 85));
	itemPos.depth.shrinked.setP1(QPointF(0, 0));
	itemPos.depth.shrinked.setP2(QPointF(0, 55));
	itemPos.depth.intermediate.setP1(QPointF(0, 0));
	itemPos.depth.intermediate.setP2(QPointF(0, 65));

	// Time Axis Config
	itemPos.time.pos.on.setX(3);
	itemPos.time.pos.on.setY(95);
#ifdef SUBSURFACE_MOBILE
	itemPos.time.pos.on.setY(89);
#endif
	itemPos.time.pos.off.setX(3);
	itemPos.time.pos.off.setY(110);
	itemPos.time.expanded.setP1(QPointF(0, 0));
	itemPos.time.expanded.setP2(QPointF(94, 0));

	// Partial Gas Axis Config
	itemPos.partialPressure.pos.on.setX(97);
	itemPos.partialPressure.pos.on.setY(75);
	itemPos.partialPressure.pos.off.setX(110);
	itemPos.partialPressure.pos.off.setY(63);
	itemPos.partialPressure.expanded.setP1(QPointF(0, 0));
	itemPos.partialPressure.expanded.setP2(QPointF(0, 19));
	itemPos.partialPressureWithTankBar = itemPos.partialPressure;
	itemPos.partialPressureWithTankBar.expanded.setP2(QPointF(0, 17));
	itemPos.partialPressureTissue = itemPos.partialPressure;
	itemPos.partialPressureTissue.pos.on.setX(97);
	itemPos.partialPressureTissue.pos.on.setY(65);
	itemPos.partialPressureTissue.expanded.setP2(QPointF(0, 16));

	// cylinder axis config
	itemPos.cylinder.pos.on.setX(3);
	itemPos.cylinder.pos.on.setY(20);
	itemPos.cylinder.pos.off.setX(-10);
	itemPos.cylinder.pos.off.setY(20);
	itemPos.cylinder.expanded.setP1(QPointF(0, 15));
	itemPos.cylinder.expanded.setP2(QPointF(0, 50));
	itemPos.cylinder.shrinked.setP1(QPointF(0, 0));
	itemPos.cylinder.shrinked.setP2(QPointF(0, 20));
	itemPos.cylinder.intermediate.setP1(QPointF(0, 0));
	itemPos.cylinder.intermediate.setP2(QPointF(0, 20));

	// Temperature axis config
	itemPos.temperature.pos.on.setX(3);
	itemPos.temperature.pos.on.setY(60);
#ifdef SUBSURFACE_MOBILE
	itemPos.temperature.pos.on.setY(50);
#endif
	itemPos.temperatureAll.pos.on.setY(51);
	itemPos.temperature.pos.off.setX(-10);
	itemPos.temperature.pos.off.setY(40);
	itemPos.temperature.expanded.setP1(QPointF(0, 20));
	itemPos.temperature.expanded.setP2(QPointF(0, 33));
	itemPos.temperature.shrinked.setP1(QPointF(0, 2));
	itemPos.temperature.shrinked.setP2(QPointF(0, 12));
	itemPos.temperature.intermediate.setP1(QPointF(0, 2));
	itemPos.temperature.intermediate.setP2(QPointF(0, 12));

	// Heartbeat axis config
	itemPos.heartBeat.pos.on.setX(3);
	itemPos.heartBeat.pos.on.setY(82);
	itemPos.heartBeat.expanded.setP1(QPointF(0, 0));
	itemPos.heartBeat.expanded.setP2(QPointF(0, 10));
	itemPos.heartBeatWithTankBar = itemPos.heartBeat;
	itemPos.heartBeatWithTankBar.expanded.setP2(QPointF(0, 7));

	// Percentage axis config
	itemPos.percentage.pos.on.setX(3);
	itemPos.percentage.pos.on.setY(80);
	itemPos.percentage.expanded.setP1(QPointF(0, 0));
	itemPos.percentage.expanded.setP2(QPointF(0, 15));
	itemPos.percentageWithTankBar = itemPos.percentage;
	itemPos.percentageWithTankBar.expanded.setP2(QPointF(0, 12));

	itemPos.dcLabel.on.setX(3);
	itemPos.dcLabel.on.setY(100);
	itemPos.dcLabel.off.setX(-10);
	itemPos.dcLabel.off.setY(100);

	itemPos.tankBar.on.setX(0);
	itemPos.tankBar.on.setY(91.5);
}

void ProfileWidget2::setupItem(AbstractProfilePolygonItem *item, DiveCartesianAxis *hAxis,
			       DiveCartesianAxis *vAxis, DivePlotDataModel *model,
			       int vData, int hData, int zValue)
{
	item->setHorizontalAxis(hAxis);
	item->setVerticalAxis(vAxis);
	item->setModel(model);
	item->setVerticalDataColumn(vData);
	item->setHorizontalDataColumn(hData);
	item->setZValue(zValue);
}

void ProfileWidget2::setupSceneAndFlags()
{
	setScene(new QGraphicsScene(this));
	scene()->setSceneRect(0, 0, 100, 100);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scene()->setItemIndexMethod(QGraphicsScene::NoIndex);
	setOptimizationFlags(QGraphicsView::DontSavePainterState);
	setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
	setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	setMouseTracking(true);
	background->setFlag(QGraphicsItem::ItemIgnoresTransformations);
}

void ProfileWidget2::resetZoom()
{
	if (!zoomLevel)
		return;
	const qreal defScale = 1.0 / qPow(zoomFactor, (qreal)zoomLevel);
	scale(defScale, defScale);
	zoomLevel = 0;
}

// Currently just one dive, but the plan is to enable All of the selected dives.
void ProfileWidget2::plotDive(struct dive *d, bool force)
{
	static bool firstCall = true;
#ifndef SUBSURFACE_MOBILE
	QTime measureDuration; // let's measure how long this takes us (maybe we'll turn of TTL calculation later
	measureDuration.start();
#endif
	if (currentState != ADD && currentState != PLAN) {
		if (!d) {
			if (selected_dive == -1)
				return;
			d = current_dive; // display the current dive
		}

		// No need to do this again if we are already showing the same dive
		// computer of the same dive, so we check the unique id of the dive
		// and the selected dive computer number against the ones we are
		// showing (can't compare the dive pointers as those might change).
		if (d->id == displayed_dive.id && dc_number == dataModel->dcShown() && !force)
			return;

		// this copies the dive and makes copies of all the relevant additional data
		copy_dive(d, &displayed_dive);
#ifndef SUBSURFACE_MOBILE
		if (decoMode() == VPMB)
			decoModelParameters->setText(QString("VPM-B +%1").arg(prefs.vpmb_conservatism));
		else
			decoModelParameters->setText(QString("GF %1/%2").arg(prefs.gflow).arg(prefs.gfhigh));
	} else {
		DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
		plannerModel->createTemporaryPlan();
		struct diveplan &diveplan = plannerModel->getDiveplan();
		if (!diveplan.dp) {
			plannerModel->deleteTemporaryPlan();
			return;
		}
		if (decoMode() == VPMB)
			decoModelParameters->setText(QString("VPM-B +%1").arg(diveplan.vpmb_conservatism));
		else
			decoModelParameters->setText(QString("GF %1/%2").arg(diveplan.gflow).arg(diveplan.gfhigh));
#endif
	}

	// special handling for the first time we display things
	int animSpeedBackup = 0;
	if (firstCall && haveFilesOnCommandLine()) {
		animSpeedBackup = prefs.animation_speed;
		prefs.animation_speed = 0;
		firstCall = false;
	}

	// restore default zoom level
	resetZoom();

#ifndef SUBSURFACE_MOBILE
	// reset some item visibility on printMode changes
	toolTipItem->setVisible(!printMode);
	rulerItem->setVisible(prefs.rulergraph && !printMode && currentState != PLAN && currentState != ADD);
#endif
	if (currentState == EMPTY)
		setProfileState();

	// next get the dive computer structure - if there are no samples
	// let's create a fake profile that's somewhat reasonable for the
	// data that we have
	struct divecomputer *currentdc = select_dc(&displayed_dive);
	Q_ASSERT(currentdc);
	if (!currentdc || !currentdc->samples) {
		currentdc = fake_dc(currentdc, false);
	}

#ifndef SUBSURFACE_MOBILE
	bool setpointflag = (currentdc->divemode == CCR) && prefs.pp_graphs.po2 && current_dive;
	bool sensorflag = setpointflag && prefs.show_ccr_sensors;
	o2SetpointGasItem->setVisible(setpointflag && prefs.show_ccr_setpoint);
	ccrsensor1GasItem->setVisible(sensorflag);
	ccrsensor2GasItem->setVisible(sensorflag && (currentdc->no_o2sensors > 1));
	ccrsensor3GasItem->setVisible(sensorflag && (currentdc->no_o2sensors > 2));
#endif

	/* This struct holds all the data that's about to be plotted.
	 * I'm not sure this is the best approach ( but since we are
	 * interpolating some points of the Dive, maybe it is... )
	 * The  Calculation of the points should be done per graph,
	 * so I'll *not* calculate everything if something is not being
	 * shown.
	 */
	plotInfo = calculate_max_limits_new(&displayed_dive, currentdc);
	create_plot_info_new(&displayed_dive, currentdc, &plotInfo, !shouldCalculateMaxDepth);
	if (shouldCalculateMaxTime)
		maxtime = get_maxtime(&plotInfo);

	/* Only update the max depth if it's bigger than the current ones
	 * when we are dragging the handler to plan / add dive.
	 * otherwhise, update normally.
	 */
	int newMaxDepth = get_maxdepth(&plotInfo);
	if (!shouldCalculateMaxDepth) {
		if (maxdepth < newMaxDepth) {
			maxdepth = newMaxDepth;
		}
	} else {
		maxdepth = newMaxDepth;
	}

	dataModel->setDive(&displayed_dive, plotInfo);
#ifndef SUBSURFACE_MOBILE
	toolTipItem->setPlotInfo(plotInfo);
#endif
	// It seems that I'll have a lot of boilerplate setting the model / axis for
	// each item, I'll mostly like to fix this in the future, but I'll keep at this for now.
	profileYAxis->setMaximum(maxdepth);
	profileYAxis->updateTicks();

	temperatureAxis->setMinimum(plotInfo.mintemp);
	temperatureAxis->setMaximum(plotInfo.maxtemp - plotInfo.mintemp > 2000 ? plotInfo.maxtemp : plotInfo.mintemp + 2000);

#ifndef SUBSURFACE_MOBILE
	if (plotInfo.maxhr) {
		heartBeatAxis->setMinimum(plotInfo.minhr);
		heartBeatAxis->setMaximum(plotInfo.maxhr);
		heartBeatAxis->updateTicks(HR_AXIS); // this shows the ticks
	}
	heartBeatAxis->setVisible(prefs.hrgraph && plotInfo.maxhr);

	percentageAxis->setMinimum(0);
	percentageAxis->setMaximum(100);
	percentageAxis->setVisible(false);
	percentageAxis->updateTicks(HR_AXIS);
#endif
	timeAxis->setMaximum(maxtime);
	int i, incr;
	static int increments[8] = { 10, 20, 30, 60, 5 * 60, 10 * 60, 15 * 60, 30 * 60 };
	/* Time markers: at most every 10 seconds, but no more than 12 markers.
	 * We start out with 10 seconds and increment up to 30 minutes,
	 * depending on the dive time.
	 * This allows for 6h dives - enough (I hope) for even the craziest
	 * divers - but just in case, for those 8h depth-record-breaking dives,
	 * we double the interval if this still doesn't get us to 12 or fewer
	 * time markers */
	i = 0;
	while (i < 7 && maxtime / increments[i] > 12)
		i++;
	incr = increments[i];
	while (maxtime / incr > 12)
		incr *= 2;
	timeAxis->setTickInterval(incr);
	timeAxis->updateTicks();
	cylinderPressureAxis->setMinimum(plotInfo.minpressure);
	cylinderPressureAxis->setMaximum(plotInfo.maxpressure);
#ifndef SUBSURFACE_MOBILE
	rulerItem->setPlotInfo(plotInfo);
#endif
	tankItem->setData(dataModel, &plotInfo, &displayed_dive);

	dataModel->emitDataChanged();
	// The event items are a bit special since we don't know how many events are going to
	// exist on a dive, so I cant create cache items for that. that's why they are here
	// while all other items are up there on the constructor.
	qDeleteAll(eventItems);
	eventItems.clear();
	struct event *event = currentdc->events;
	while (event) {
		// if print mode is selected only draw headings, SP change, gas events or bookmark event
		if (printMode) {
			if (same_string(event->name, "") ||
			    !(strcmp(event->name, "heading") == 0 ||
			      (same_string(event->name, "SP change") && event->time.seconds == 0) ||
			      event_is_gaschange(event) ||
			      event->type == SAMPLE_EVENT_BOOKMARK)) {
				event = event->next;
				continue;
			}
		}
		DiveEventItem *item = new DiveEventItem();
		item->setHorizontalAxis(timeAxis);
		item->setVerticalAxis(profileYAxis);
		item->setModel(dataModel);
		item->setEvent(event);
		item->setZValue(2);
		scene()->addItem(item);
		eventItems.push_back(item);
		event = event->next;
	}
	// Only set visible the events that should be visible
	Q_FOREACH (DiveEventItem *event, eventItems) {
		event->setVisible(!event->shouldBeHidden());
	}
	QString dcText = get_dc_nickname(currentdc->model, currentdc->deviceid);
#ifndef SUBSURFACE_MOBILE
	int nr;
	if ((nr = number_of_computers(&displayed_dive)) > 1)
		dcText += tr(" (#%1 of %2)").arg(dc_number + 1).arg(nr);
#endif
	if (dcText.isEmpty())
		dcText = tr("Unknown dive computer");
	diveComputerText->setText(dcText);
	if (haveFilesOnCommandLine() && animSpeedBackup != 0) {
		prefs.animation_speed = animSpeedBackup;
	}

#ifndef SUBSURFACE_MOBILE
	if (currentState == ADD || currentState == PLAN) { // TODO: figure a way to move this from here.
		repositionDiveHandlers();
		DivePlannerPointsModel *model = DivePlannerPointsModel::instance();
		model->deleteTemporaryPlan();
	}
#endif
	plotPictures();

	// OK, how long did this take us? Anything above the second is way too long,
	// so if we are calculation TTS / NDL then let's force that off.
#ifndef SUBSURFACE_MOBILE
	if (measureDuration.elapsed() > 1000 && prefs.calcndltts) {
		SettingsObjectWrapper::instance()->techDetails->setCalcndltts(false);
		report_error(qPrintable(tr("Show NDL / TTS was disabled because of excessive processing time")));
	}
#endif
	emit showError();
}

void ProfileWidget2::recalcCeiling()
{
#ifndef SUBSURFACE_MOBILE
	diveCeiling->recalc();
#endif
}

void ProfileWidget2::dateTimeChanged()
{
	emit dateTimeChangedItems();
}

void ProfileWidget2::actionRequestedReplot(bool triggered)
{
	Q_UNUSED(triggered);
	settingsChanged();
}

void ProfileWidget2::settingsChanged()
{
	// if we are showing calculated ceilings then we have to replot()
	// because the GF could have changed; otherwise we try to avoid replot()
	bool needReplot = prefs.calcceiling;
#ifndef SUBSURFACE_MOBILE
	if ((prefs.percentagegraph||prefs.hrgraph) && PP_GRAPHS_ENABLED) {
		profileYAxis->animateChangeLine(itemPos.depth.shrinked);
		temperatureAxis->setPos(itemPos.temperatureAll.pos.on);
		temperatureAxis->animateChangeLine(itemPos.temperature.shrinked);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.shrinked);

		if (prefs.tankbar) {
			percentageAxis->setPos(itemPos.percentageWithTankBar.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentageWithTankBar.expanded);
			heartBeatAxis->setPos(itemPos.heartBeatWithTankBar.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeatWithTankBar.expanded);
		}else {
			percentageAxis->setPos(itemPos.percentage.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentage.expanded);
			heartBeatAxis->setPos(itemPos.heartBeat.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeat.expanded);
		}
		gasYAxis->setPos(itemPos.partialPressureTissue.pos.on);
		gasYAxis->animateChangeLine(itemPos.partialPressureTissue.expanded);

	} else if (PP_GRAPHS_ENABLED || prefs.hrgraph || prefs.percentagegraph) {
		profileYAxis->animateChangeLine(itemPos.depth.intermediate);
		temperatureAxis->setPos(itemPos.temperature.pos.on);
		temperatureAxis->animateChangeLine(itemPos.temperature.intermediate);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.intermediate);
		if (prefs.tankbar) {
			percentageAxis->setPos(itemPos.percentageWithTankBar.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentageWithTankBar.expanded);
			gasYAxis->setPos(itemPos.partialPressureWithTankBar.pos.on);
			gasYAxis->setLine(itemPos.partialPressureWithTankBar.expanded);
			heartBeatAxis->setPos(itemPos.heartBeatWithTankBar.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeatWithTankBar.expanded);
		} else {
			gasYAxis->setPos(itemPos.partialPressure.pos.on);
			gasYAxis->animateChangeLine(itemPos.partialPressure.expanded);
			percentageAxis->setPos(itemPos.percentage.pos.on);
			percentageAxis->setLine(itemPos.percentage.expanded);
			heartBeatAxis->setPos(itemPos.heartBeat.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeat.expanded);
		}
	} else {
#else
	{
#endif
		profileYAxis->animateChangeLine(itemPos.depth.expanded);
		if (prefs.tankbar) {
			temperatureAxis->setPos(itemPos.temperatureAll.pos.on);
		} else {
			temperatureAxis->setPos(itemPos.temperature.pos.on);
		}
		temperatureAxis->animateChangeLine(itemPos.temperature.expanded);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.expanded);
	}

	tankItem->setVisible(prefs.tankbar);
	if (prefs.zoomed_plot != isPlotZoomed) {
		isPlotZoomed = prefs.zoomed_plot;
		needReplot = true;
	}
	if (needReplot)
		replot();
}

void ProfileWidget2::resizeEvent(QResizeEvent *event)
{
	QGraphicsView::resizeEvent(event);
	fitInView(sceneRect(), Qt::IgnoreAspectRatio);
	fixBackgroundPos();
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::mousePressEvent(QMouseEvent *event)
{
	if (zoomLevel)
		return;
	QGraphicsView::mousePressEvent(event);
	if (currentState == PLAN)
		shouldCalculateMaxTime = false;
}

void ProfileWidget2::divePlannerHandlerClicked()
{
	if (zoomLevel)
		return;
	shouldCalculateMaxDepth = false;
	replot();
}

void ProfileWidget2::divePlannerHandlerReleased()
{
	if (zoomLevel)
		return;
	shouldCalculateMaxDepth = true;
	replot();
}

void ProfileWidget2::mouseReleaseEvent(QMouseEvent *event)
{
	if (zoomLevel)
		return;
	QGraphicsView::mouseReleaseEvent(event);
	if (currentState == PLAN) {
		shouldCalculateMaxTime = true;
		replot();
	}
}
#endif

void ProfileWidget2::fixBackgroundPos()
{
	static QPixmap toBeScaled(backgroundFile);
	if (currentState != EMPTY)
		return;
	QPixmap p = toBeScaled.scaledToHeight(viewport()->height() - 40, Qt::SmoothTransformation);
	int x = viewport()->width() / 2 - p.width() / 2;
	int y = viewport()->height() / 2 - p.height() / 2;
	background->setPixmap(p);
	background->setX(mapToScene(x, 0).x());
	background->setY(mapToScene(y, 20).y());
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::wheelEvent(QWheelEvent *event)
{
	if (currentState == EMPTY)
		return;
	QPoint toolTipPos = mapFromScene(toolTipItem->pos());
	if (event->buttons() == Qt::LeftButton)
		return;
	if (event->delta() > 0 && zoomLevel < 20) {
		scale(zoomFactor, zoomFactor);
		zoomLevel++;
	} else if (event->delta() < 0 && zoomLevel > 0) {
		// Zooming out
		scale(1.0 / zoomFactor, 1.0 / zoomFactor);
		zoomLevel--;
	}
	scrollViewTo(event->pos());
	toolTipItem->setPos(mapToScene(toolTipPos));
}

void ProfileWidget2::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (currentState == PLAN || currentState == ADD) {
		DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
		QPointF mappedPos = mapToScene(event->pos());
		if (isPointOutOfBoundaries(mappedPos))
			return;

		int minutes = rint(timeAxis->valueAt(mappedPos) / 60);
		int milimeters = rint(profileYAxis->valueAt(mappedPos) / M_OR_FT(1, 1)) * M_OR_FT(1, 1);
		plannerModel->addStop(milimeters, minutes * 60, 0, 0, true);
	}
}

bool ProfileWidget2::isPointOutOfBoundaries(const QPointF &point) const
{
	double xpos = timeAxis->valueAt(point);
	double ypos = profileYAxis->valueAt(point);
	return (xpos > timeAxis->maximum() ||
		xpos < timeAxis->minimum() ||
		ypos > profileYAxis->maximum() ||
		ypos < profileYAxis->minimum());
}

void ProfileWidget2::scrollViewTo(const QPoint &pos)
{
	/* since we cannot use translate() directly on the scene we hack on
 * the scroll bars (hidden) functionality */
	if (!zoomLevel || currentState == EMPTY)
		return;
	QScrollBar *vs = verticalScrollBar();
	QScrollBar *hs = horizontalScrollBar();
	const qreal yRat = (qreal)pos.y() / viewport()->height();
	const qreal xRat = (qreal)pos.x() / viewport()->width();
	vs->setValue(yRat * vs->maximum());
	hs->setValue(xRat * hs->maximum());
}

void ProfileWidget2::mouseMoveEvent(QMouseEvent *event)
{
	QPointF pos = mapToScene(event->pos());
	toolTipItem->refresh(pos);
	if (zoomLevel == 0) {
		QGraphicsView::mouseMoveEvent(event);
	} else {
		QPoint toolTipPos = mapFromScene(toolTipItem->pos());
		scrollViewTo(event->pos());
		toolTipItem->setPos(mapToScene(toolTipPos));
	}

	qreal vValue = profileYAxis->valueAt(pos);
	qreal hValue = timeAxis->valueAt(pos);

	if (profileYAxis->maximum() >= vValue && profileYAxis->minimum() <= vValue) {
		mouseFollowerHorizontal->setPos(timeAxis->pos().x(), pos.y());
	}
	if (timeAxis->maximum() >= hValue && timeAxis->minimum() <= hValue) {
		mouseFollowerVertical->setPos(pos.x(), profileYAxis->line().y1());
	}
}

bool ProfileWidget2::eventFilter(QObject *object, QEvent *event)
{
	QGraphicsScene *s = qobject_cast<QGraphicsScene *>(object);
	if (s && event->type() == QEvent::GraphicsSceneHelp) {
		event->ignore();
		return true;
	}
	return QGraphicsView::eventFilter(object, event);
}
#endif

void ProfileWidget2::setEmptyState()
{
	// Then starting Empty State, move the background up.
	if (currentState == EMPTY)
		return;

	disconnectTemporaryConnections();
	setBackgroundBrush(getColor(::BACKGROUND, isGrayscale));
	dataModel->clear();
	currentState = EMPTY;
	emit enableToolbar(false);

	fixBackgroundPos();
	background->setVisible(true);

	profileYAxis->setVisible(false);
	gasYAxis->setVisible(false);
	timeAxis->setVisible(false);
	temperatureAxis->setVisible(false);
	cylinderPressureAxis->setVisible(false);
	diveComputerText->setVisible(false);
	reportedCeiling->setVisible(false);
	tankItem->setVisible(false);
#ifndef SUBSURFACE_MOBILE
	toolTipItem->setVisible(false);
	diveCeiling->setVisible(false);
	decoModelParameters->setVisible(false);
	rulerItem->setVisible(false);
	pn2GasItem->setVisible(false);
	po2GasItem->setVisible(false);
	o2SetpointGasItem->setVisible(false);
	ccrsensor1GasItem->setVisible(false);
	ccrsensor2GasItem->setVisible(false);
	ccrsensor3GasItem->setVisible(false);
	pheGasItem->setVisible(false);
	ambPressureItem->setVisible(false);
	gflineItem->setVisible(false);
	mouseFollowerHorizontal->setVisible(false);
	mouseFollowerVertical->setVisible(false);
	heartBeatAxis->setVisible(false);
	heartBeatItem->setVisible(false);
#endif

#define HIDE_ALL(TYPE, CONTAINER) \
	Q_FOREACH (TYPE *item, CONTAINER) item->setVisible(false);
#ifndef SUBSURFACE_MOBILE
	HIDE_ALL(DiveCalculatedTissue, allTissues);
	HIDE_ALL(DivePercentageItem, allPercentages);
#endif
	HIDE_ALL(DiveEventItem, eventItems);
#ifndef SUBSURFACE_MOBILE
	HIDE_ALL(DiveHandler, handles);
#endif
	HIDE_ALL(QGraphicsSimpleTextItem, gases);
#undef HIDE_ALL
}

void ProfileWidget2::setProfileState()
{
	// Then starting Empty State, move the background up.
	if (currentState == PROFILE)
		return;

	disconnectTemporaryConnections();
	connect(DivePictureModel::instance(), SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(plotPictures()));
	connect(DivePictureModel::instance(), SIGNAL(rowsInserted(const QModelIndex &, int, int)), this, SLOT(plotPictures()));
	connect(DivePictureModel::instance(), SIGNAL(rowsRemoved(const QModelIndex &, int, int)), this, SLOT(plotPictures()));
	/* show the same stuff that the profile shows. */

	emit enableShortcuts();

	currentState = PROFILE;
	emit enableToolbar(true);
	setBackgroundBrush(getColor(::BACKGROUND, isGrayscale));

	background->setVisible(false);
	profileYAxis->setVisible(true);
	gasYAxis->setVisible(true);
	timeAxis->setVisible(true);
	temperatureAxis->setVisible(true);
	cylinderPressureAxis->setVisible(true);

	profileYAxis->setPos(itemPos.depth.pos.on);
#ifndef SUBSURFACE_MOBILE
	toolTipItem->readPos();
	toolTipItem->setVisible(true);
	if ((prefs.percentagegraph||prefs.hrgraph) && PP_GRAPHS_ENABLED) {
		profileYAxis->animateChangeLine(itemPos.depth.shrinked);
		temperatureAxis->setPos(itemPos.temperatureAll.pos.on);
		temperatureAxis->animateChangeLine(itemPos.temperature.shrinked);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.shrinked);

		if (prefs.tankbar) {
			percentageAxis->setPos(itemPos.percentageWithTankBar.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentageWithTankBar.expanded);
			heartBeatAxis->setPos(itemPos.heartBeatWithTankBar.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeatWithTankBar.expanded);
		}else {
			percentageAxis->setPos(itemPos.percentage.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentage.expanded);
			heartBeatAxis->setPos(itemPos.heartBeat.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeat.expanded);
		}
		gasYAxis->setPos(itemPos.partialPressureTissue.pos.on);
		gasYAxis->animateChangeLine(itemPos.partialPressureTissue.expanded);

	} else if (PP_GRAPHS_ENABLED || prefs.hrgraph || prefs.percentagegraph) {
		profileYAxis->animateChangeLine(itemPos.depth.intermediate);
		temperatureAxis->setPos(itemPos.temperature.pos.on);
		temperatureAxis->animateChangeLine(itemPos.temperature.intermediate);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.intermediate);
		if (prefs.tankbar) {
			percentageAxis->setPos(itemPos.percentageWithTankBar.pos.on);
			percentageAxis->animateChangeLine(itemPos.percentageWithTankBar.expanded);
			gasYAxis->setPos(itemPos.partialPressureWithTankBar.pos.on);
			gasYAxis->setLine(itemPos.partialPressureWithTankBar.expanded);
			heartBeatAxis->setPos(itemPos.heartBeatWithTankBar.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeatWithTankBar.expanded);
		} else {
			gasYAxis->setPos(itemPos.partialPressure.pos.on);
			gasYAxis->animateChangeLine(itemPos.partialPressure.expanded);
			percentageAxis->setPos(itemPos.percentage.pos.on);
			percentageAxis->setLine(itemPos.percentage.expanded);
			heartBeatAxis->setPos(itemPos.heartBeat.pos.on);
			heartBeatAxis->animateChangeLine(itemPos.heartBeat.expanded);
		}
	} else {
#else
	{
#endif
		profileYAxis->animateChangeLine(itemPos.depth.expanded);
		if (prefs.tankbar) {
			temperatureAxis->setPos(itemPos.temperatureAll.pos.on);
		} else {
			temperatureAxis->setPos(itemPos.temperature.pos.on);
		}
		temperatureAxis->animateChangeLine(itemPos.temperature.expanded);
		cylinderPressureAxis->animateChangeLine(itemPos.cylinder.expanded);
	}
#ifndef SUBSURFACE_MOBILE
	pn2GasItem->setVisible(prefs.pp_graphs.pn2);
	po2GasItem->setVisible(prefs.pp_graphs.po2);
	pheGasItem->setVisible(prefs.pp_graphs.phe);

	bool setpointflag = current_dive && (current_dc->divemode == CCR) && prefs.pp_graphs.po2;
	bool sensorflag = setpointflag && prefs.show_ccr_sensors;
	o2SetpointGasItem->setVisible(setpointflag && prefs.show_ccr_setpoint);
	ccrsensor1GasItem->setVisible(sensorflag);
	ccrsensor2GasItem->setVisible(sensorflag && (current_dc->no_o2sensors > 1));
	ccrsensor3GasItem->setVisible(sensorflag && (current_dc->no_o2sensors > 2));

	heartBeatItem->setVisible(prefs.hrgraph);
	diveCeiling->setVisible(prefs.calcceiling);
	decoModelParameters->setVisible(prefs.calcceiling);

	if (prefs.calcalltissues) {
		Q_FOREACH (DiveCalculatedTissue *tissue, allTissues) {
			tissue->setVisible(true);
		}
	}
	if (prefs.percentagegraph) {
		Q_FOREACH (DivePercentageItem *percentage, allPercentages) {
			percentage->setVisible(true);
		}
	}

	rulerItem->setVisible(prefs.rulergraph);

#endif
	timeAxis->setPos(itemPos.time.pos.on);
	timeAxis->setLine(itemPos.time.expanded);

	cylinderPressureAxis->setPos(itemPos.cylinder.pos.on);
	meanDepthItem->setVisible(prefs.show_average_depth);

	diveComputerText->setVisible(true);
	diveComputerText->setPos(itemPos.dcLabel.on);

	reportedCeiling->setVisible(prefs.dcceiling);

	tankItem->setVisible(prefs.tankbar);
	tankItem->setPos(itemPos.tankBar.on);

#define HIDE_ALL(TYPE, CONTAINER) \
	Q_FOREACH (TYPE *item, CONTAINER) item->setVisible(false);

#ifndef SUBSURFACE_MOBILE
	HIDE_ALL(DiveHandler, handles);
	mouseFollowerHorizontal->setVisible(false);
	mouseFollowerVertical->setVisible(false);
#endif
	HIDE_ALL(QGraphicsSimpleTextItem, gases);
#undef HIDE_ALL
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::clearHandlers()
{
	if (handles.count()) {
		foreach (DiveHandler *handle, handles) {
			scene()->removeItem(handle);
			delete handle;
		}
		handles.clear();
	}
}

void ProfileWidget2::setToolTipVisibile(bool visible)
{
	toolTipItem->setVisible(visible);
}

void ProfileWidget2::setAddState()
{
	if (currentState == ADD)
		return;

	clearHandlers();
	setProfileState();
	mouseFollowerHorizontal->setVisible(true);
	mouseFollowerVertical->setVisible(true);
	mouseFollowerHorizontal->setLine(timeAxis->line());
	mouseFollowerVertical->setLine(QLineF(0, profileYAxis->pos().y(), 0, timeAxis->pos().y()));
	disconnectTemporaryConnections();
	emit disableShortcuts(false);
	actionsForKeys[Qt::Key_Left]->setShortcut(Qt::Key_Left);
	actionsForKeys[Qt::Key_Right]->setShortcut(Qt::Key_Right);
	actionsForKeys[Qt::Key_Up]->setShortcut(Qt::Key_Up);
	actionsForKeys[Qt::Key_Down]->setShortcut(Qt::Key_Down);
	actionsForKeys[Qt::Key_Escape]->setShortcut(Qt::Key_Escape);
	actionsForKeys[Qt::Key_Delete]->setShortcut(Qt::Key_Delete);

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	connect(plannerModel, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(replot()));
	connect(plannerModel, SIGNAL(cylinderModelEdited()), this, SLOT(replot()));
#ifndef SUBSURFACE_MOBILE
	connect(plannerModel, SIGNAL(rowsInserted(const QModelIndex &, int, int)),
		this, SLOT(pointInserted(const QModelIndex &, int, int)));
	connect(plannerModel, SIGNAL(rowsRemoved(const QModelIndex &, int, int)),
		this, SLOT(pointsRemoved(const QModelIndex &, int, int)));
#endif
	/* show the same stuff that the profile shows. */
	currentState = ADD; /* enable the add state. */
	diveCeiling->setVisible(true);
	decoModelParameters->setVisible(true);
	setBackgroundBrush(QColor("#A7DCFF"));
}

void ProfileWidget2::setPlanState()
{
	if (currentState == PLAN)
		return;

	setProfileState();
	mouseFollowerHorizontal->setVisible(true);
	mouseFollowerVertical->setVisible(true);
	mouseFollowerHorizontal->setLine(timeAxis->line());
	mouseFollowerVertical->setLine(QLineF(0, profileYAxis->pos().y(), 0, timeAxis->pos().y()));
	disconnectTemporaryConnections();
	emit disableShortcuts(true);
	actionsForKeys[Qt::Key_Left]->setShortcut(Qt::Key_Left);
	actionsForKeys[Qt::Key_Right]->setShortcut(Qt::Key_Right);
	actionsForKeys[Qt::Key_Up]->setShortcut(Qt::Key_Up);
	actionsForKeys[Qt::Key_Down]->setShortcut(Qt::Key_Down);
	actionsForKeys[Qt::Key_Escape]->setShortcut(Qt::Key_Escape);
	actionsForKeys[Qt::Key_Delete]->setShortcut(Qt::Key_Delete);

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	connect(plannerModel, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(replot()));
	connect(plannerModel, SIGNAL(cylinderModelEdited()), this, SLOT(replot()));
	connect(plannerModel, SIGNAL(rowsInserted(const QModelIndex &, int, int)),
		this, SLOT(pointInserted(const QModelIndex &, int, int)));
	connect(plannerModel, SIGNAL(rowsRemoved(const QModelIndex &, int, int)),
		this, SLOT(pointsRemoved(const QModelIndex &, int, int)));
	/* show the same stuff that the profile shows. */
	currentState = PLAN; /* enable the add state. */
	diveCeiling->setVisible(true);
	decoModelParameters->setVisible(true);
	setBackgroundBrush(QColor("#D7E3EF"));
}
#endif

extern struct ev_select *ev_namelist;
extern int evn_allocated;
extern int evn_used;

bool ProfileWidget2::isPlanner()
{
	return currentState == PLAN;
}

bool ProfileWidget2::isAddOrPlanner()
{
	return currentState == PLAN || currentState == ADD;
}

struct plot_data *ProfileWidget2::getEntryFromPos(QPointF pos)
{
	// find the time stamp corresponding to the mouse position
	int seconds = timeAxis->valueAt(pos);
	struct plot_data *entry = NULL;

	for (int i = 0; i < plotInfo.nr; i++) {
		entry = plotInfo.entry + i;
		if ((int)entry->sec >= seconds)
			break;
	}
	return entry;
}

void ProfileWidget2::setReplot(bool state)
{
	replotEnabled = state;
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::contextMenuEvent(QContextMenuEvent *event)
{
	if (currentState == ADD || currentState == PLAN) {
		QGraphicsView::contextMenuEvent(event);
		return;
	}
	QMenu m;
	bool isDCName = false;
	if (selected_dive == -1)
		return;
	// figure out if we are ontop of the dive computer name in the profile
	QGraphicsItem *sceneItem = itemAt(mapFromGlobal(event->globalPos()));
	if (sceneItem) {
		QGraphicsItem *parentItem = sceneItem;
		while (parentItem) {
			if (parentItem->data(SUBSURFACE_OBJ_DATA) == SUBSURFACE_OBJ_DC_TEXT) {
				isDCName = true;
				break;
			}
			parentItem = parentItem->parentItem();
		}
		if (isDCName) {
			if (dc_number == 0 && count_divecomputers() == 1)
				// nothing to do, can't delete or reorder
				return;
			// create menu to show when right clicking on dive computer name
			if (dc_number > 0)
				m.addAction(tr("Make first divecomputer"), this, SLOT(makeFirstDC()));
			if (count_divecomputers() > 1)
				m.addAction(tr("Delete this divecomputer"), this, SLOT(deleteCurrentDC()));
			m.exec(event->globalPos());
			// don't show the regular profile context menu
			return;
		}
	}
	// create the profile context menu
	QPointF scenePos = mapToScene(event->pos());
	struct plot_data *entry = getEntryFromPos(scenePos);
	GasSelectionModel *model = GasSelectionModel::instance();
	model->repopulate();
	int rowCount = model->rowCount();
	if (rowCount > 1) {
		// if we have more than one gas, offer to switch to another one
		QMenu *gasChange = m.addMenu(tr("Add gas change"));
		for (int i = 0; i < rowCount; i++) {
			QAction *action = new QAction(&m);
			action->setText(model->data(model->index(i, 0), Qt::DisplayRole).toString() + QString(tr(" (Tank %1)")).arg(i + 1));
			connect(action, SIGNAL(triggered(bool)), this, SLOT(changeGas()));
			action->setData(event->globalPos());
			if (i == entry->cylinderindex)
				action->setDisabled(true);
			gasChange->addAction(action);
		}
	}
	QAction *setpointAction = m.addAction(tr("Add set-point change"), this, SLOT(addSetpointChange()));
	setpointAction->setData(event->globalPos());
	QAction *action = m.addAction(tr("Add bookmark"), this, SLOT(addBookmark()));
	action->setData(event->globalPos());

	if (same_string(current_dc->model, "manually added dive"))
		m.addAction(tr("Edit the profile"), this, SIGNAL(editCurrentDive()));

	if (DiveEventItem *item = dynamic_cast<DiveEventItem *>(sceneItem)) {
		action = new QAction(&m);
		action->setText(tr("Remove event"));
		action->setData(QVariant::fromValue<void *>(item)); // so we know what to remove.
		connect(action, SIGNAL(triggered(bool)), this, SLOT(removeEvent()));
		m.addAction(action);
		action = new QAction(&m);
		action->setText(tr("Hide similar events"));
		action->setData(QVariant::fromValue<void *>(item));
		connect(action, SIGNAL(triggered(bool)), this, SLOT(hideEvents()));
		m.addAction(action);
		struct event *dcEvent = item->getEvent();
		if (dcEvent->type == SAMPLE_EVENT_BOOKMARK) {
			action = new QAction(&m);
			action->setText(tr("Edit name"));
			action->setData(QVariant::fromValue<void *>(item));
			connect(action, SIGNAL(triggered(bool)), this, SLOT(editName()));
			m.addAction(action);
		}
#if 0 // FIXME::: FINISH OR DISABLE
		// this shows how to figure out if we should ask the user if they want adjust interpolated pressures
		// at either side of a gas change
		if (dcEvent->type == SAMPLE_EVENT_GASCHANGE || dcEvent->type == SAMPLE_EVENT_GASCHANGE2) {
			qDebug() << "figure out if there are interpolated pressures";
			struct plot_data *gasChangeEntry = entry;
			struct plot_data *newGasEntry;
			while (gasChangeEntry > plotInfo.entry) {
				--gasChangeEntry;
				if (gasChangeEntry->sec <= dcEvent->time.seconds)
					break;
			}
			qDebug() << "at gas change at" << gasChangeEntry->sec << ": sensor pressure" << gasChangeEntry->pressure[0] << "interpolated" << gasChangeEntry->pressure[1];
			// now gasChangeEntry points at the gas change, that entry has the final pressure of
			// the old tank, the next entry has the starting pressure of the next tank
			if (gasChangeEntry + 1 <= plotInfo.entry + plotInfo.nr) {
				newGasEntry = gasChangeEntry + 1;
				qDebug() << "after gas change at " << newGasEntry->sec << ": sensor pressure" << newGasEntry->pressure[0] << "interpolated" << newGasEntry->pressure[1];
				if (SENSOR_PRESSURE(gasChangeEntry) == 0 || displayed_dive.cylinder[gasChangeEntry->cylinderindex].sample_start.mbar == 0) {
					// if we have no sensorpressure or if we have no pressure from samples we can assume that
					// we only have interpolated pressure (the pressure in the entry may be stored in the sensor
					// pressure field if this is the first or last entry for this tank... see details in gaspressures.c
					pressure_t pressure;
					pressure.mbar = INTERPOLATED_PRESSURE(gasChangeEntry) ? : SENSOR_PRESSURE(gasChangeEntry);
					QAction *adjustOldPressure = m.addAction(tr("Adjust pressure of tank %1 (currently interpolated as %2)")
										 .arg(gasChangeEntry->cylinderindex + 1).arg(get_pressure_string(pressure)));
				}
				if (SENSOR_PRESSURE(newGasEntry) == 0 || displayed_dive.cylinder[newGasEntry->cylinderindex].sample_start.mbar == 0) {
					// we only have interpolated press -- see commend above
					pressure_t pressure;
					pressure.mbar = INTERPOLATED_PRESSURE(newGasEntry) ? : SENSOR_PRESSURE(newGasEntry);
					QAction *adjustOldPressure = m.addAction(tr("Adjust pressure of tank %1 (currently interpolated as %2)")
										 .arg(newGasEntry->cylinderindex + 1).arg(get_pressure_string(pressure)));
				}
			}
		}
#endif
	}
	bool some_hidden = false;
	for (int i = 0; i < evn_used; i++) {
		if (ev_namelist[i].plot_ev == false) {
			some_hidden = true;
			break;
		}
	}
	if (some_hidden) {
		action = m.addAction(tr("Unhide all events"), this, SLOT(unhideEvents()));
		action->setData(event->globalPos());
	}
	m.exec(event->globalPos());
}

void ProfileWidget2::deleteCurrentDC()
{
	delete_current_divecomputer();
	mark_divelist_changed(true);
	// we need to force it since it's likely the same dive and same dc_number - but that's a different dive computer now
	plotDive(0, true);

	emit refreshDisplay(true);
}

void ProfileWidget2::makeFirstDC()
{
	make_first_dc();
	mark_divelist_changed(true);
	// this is now the first DC, so we need to redraw the profile and refresh the dive list
	// (and no, it's not just enough to rewrite the text - the first DC is special so values in the
	// dive list may change).
	// As a side benefit, this returns focus to the dive list.
	dc_number = 0;
	emit refreshDisplay(true);
}

void ProfileWidget2::hideEvents()
{
	QAction *action = qobject_cast<QAction *>(sender());
	DiveEventItem *item = static_cast<DiveEventItem *>(action->data().value<void *>());
	struct event *event = item->getEvent();

	if (QMessageBox::question(this,
				  TITLE_OR_TEXT(tr("Hide events"), tr("Hide all %1 events?").arg(event->name)),
				  QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok) {
		if (!same_string(event->name, "")) {
			for (int i = 0; i < evn_used; i++) {
				if (same_string(event->name, ev_namelist[i].ev_name)) {
					ev_namelist[i].plot_ev = false;
					break;
				}
			}
			Q_FOREACH (DiveEventItem *evItem, eventItems) {
				if (same_string(evItem->getEvent()->name, event->name))
					evItem->hide();
			}
		} else {
			item->hide();
		}
	}
}

void ProfileWidget2::unhideEvents()
{
	for (int i = 0; i < evn_used; i++) {
		ev_namelist[i].plot_ev = true;
	}
	Q_FOREACH (DiveEventItem *item, eventItems)
		item->show();
}

void ProfileWidget2::removeEvent()
{
	QAction *action = qobject_cast<QAction *>(sender());
	DiveEventItem *item = static_cast<DiveEventItem *>(action->data().value<void *>());
	struct event *event = item->getEvent();

	if (QMessageBox::question(this, TITLE_OR_TEXT(
					  tr("Remove the selected event?"),
					  tr("%1 @ %2:%3").arg(event->name).arg(event->time.seconds / 60).arg(event->time.seconds % 60, 2, 10, QChar('0'))),
				  QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok) {
		remove_event(event);
		invalidate_dive_cache(current_dive);
		mark_divelist_changed(true);
		replot();
	}
}

void ProfileWidget2::addBookmark()
{
	QAction *action = qobject_cast<QAction *>(sender());
	QPointF scenePos = mapToScene(mapFromGlobal(action->data().toPoint()));
	add_event(current_dc, timeAxis->valueAt(scenePos), SAMPLE_EVENT_BOOKMARK, 0, 0, "bookmark");
	invalidate_dive_cache(current_dive);
	mark_divelist_changed(true);
	replot();
}

void ProfileWidget2::addSetpointChange()
{
	QAction *action = qobject_cast<QAction *>(sender());
	QPointF scenePos = mapToScene(mapFromGlobal(action->data().toPoint()));
	SetpointDialog::instance()->setpointData(current_dc, timeAxis->valueAt(scenePos));
	SetpointDialog::instance()->show();
}

void ProfileWidget2::changeGas()
{
	QAction *action = qobject_cast<QAction *>(sender());
	QPointF scenePos = mapToScene(mapFromGlobal(action->data().toPoint()));
	QString gas = action->text();
	gas.remove(QRegExp(" \\(.*\\)"));

	// backup the things on the dataModel, since we will clear that out.
	struct gasmix gasmix;
	qreal sec_val = timeAxis->valueAt(scenePos);

	// no gas changes before the dive starts
	unsigned int seconds = (sec_val < 0.0) ? 0 : (unsigned int)sec_val;

	// if there is a gas change at this time stamp, remove it before adding the new one
	struct event *gasChangeEvent = current_dc->events;
	while ((gasChangeEvent = get_next_event(gasChangeEvent, "gaschange")) != NULL) {
		if (gasChangeEvent->time.seconds == seconds) {
			remove_event(gasChangeEvent);
			gasChangeEvent = current_dc->events;
		} else {
			gasChangeEvent = gasChangeEvent->next;
		}
	}
	validate_gas(gas.toUtf8().constData(), &gasmix);
	QRegExp rx("\\(\\D*(\\d+)");
	int tank;
	if (rx.indexIn(action->text()) > -1) {
		tank = rx.cap(1).toInt() - 1; // we display the tank 1 based
	} else {
		qDebug() << "failed to parse tank number";
		tank = get_gasidx(&displayed_dive, &gasmix);
	}
	// add this both to the displayed dive and the current dive
	add_gas_switch_event(current_dive, current_dc, seconds, tank);
	add_gas_switch_event(&displayed_dive, get_dive_dc(&displayed_dive, dc_number), seconds, tank);
	// this means we potentially have a new tank that is being used and needs to be shown
	fixup_dive(&displayed_dive);
	invalidate_dive_cache(current_dive);

	// FIXME - this no longer gets written to the dive list - so we need to enableEdition() here

	emit updateDiveInfo(false);
	mark_divelist_changed(true);
	replot();
}
#endif

bool ProfileWidget2::getPrintMode()
{
	return printMode;
}

void ProfileWidget2::setPrintMode(bool mode, bool grayscale)
{
	printMode = mode;
	resetZoom();

	// set printMode for axes
	profileYAxis->setPrintMode(mode);
	gasYAxis->setPrintMode(mode);
	temperatureAxis->setPrintMode(mode);
	timeAxis->setPrintMode(mode);
	cylinderPressureAxis->setPrintMode(mode);
	isGrayscale = mode ? grayscale : false;
#ifndef SUBSURFACE_MOBILE
	heartBeatAxis->setPrintMode(mode);
	percentageAxis->setPrintMode(mode);

	mouseFollowerHorizontal->setVisible(!mode);
	mouseFollowerVertical->setVisible(!mode);
	toolTipItem->setVisible(!mode);
#endif
}

void ProfileWidget2::setFontPrintScale(double scale)
{
	fontPrintScale = scale;
	emit fontPrintScaleChanged(scale);
}

double ProfileWidget2::getFontPrintScale()
{
	if (printMode)
		return fontPrintScale;
	else
		return 1.0;
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::editName()
{
	QAction *action = qobject_cast<QAction *>(sender());
	DiveEventItem *item = static_cast<DiveEventItem *>(action->data().value<void *>());
	struct event *event = item->getEvent();
	bool ok;
	QString newName = QInputDialog::getText(this, tr("Edit name of bookmark"),
						tr("Custom name:"), QLineEdit::Normal,
						event->name, &ok);
	if (ok && !newName.isEmpty()) {
		if (newName.length() > 22) { //longer names will display as garbage.
			QMessageBox lengthWarning;
			lengthWarning.setText(tr("Name is too long!"));
			lengthWarning.exec();
			return;
		}
		// order is important! first update the current dive (by matching the unchanged event),
		// then update the displayed dive (as event is part of the events on displayed dive
		// and will be freed as part of changing the name!
		update_event_name(current_dive, event, newName.toUtf8().data());
		update_event_name(&displayed_dive, event, newName.toUtf8().data());
		invalidate_dive_cache(current_dive);
		mark_divelist_changed(true);
		replot();
	}
}
#endif

void ProfileWidget2::disconnectTemporaryConnections()
{
	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	disconnect(plannerModel, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(replot()));
	disconnect(plannerModel, SIGNAL(cylinderModelEdited()), this, SLOT(replot()));

#ifndef SUBSURFACE_MOBILE
	disconnect(plannerModel, SIGNAL(rowsInserted(const QModelIndex &, int, int)),
		   this, SLOT(pointInserted(const QModelIndex &, int, int)));
	disconnect(plannerModel, SIGNAL(rowsRemoved(const QModelIndex &, int, int)),
		   this, SLOT(pointsRemoved(const QModelIndex &, int, int)));
#endif
	Q_FOREACH (QAction *action, actionsForKeys.values()) {
		action->setShortcut(QKeySequence());
		action->setShortcutContext(Qt::WidgetShortcut);
	}
}

#ifndef SUBSURFACE_MOBILE
void ProfileWidget2::pointInserted(const QModelIndex &parent, int start, int end)
{
	Q_UNUSED(parent)
	Q_UNUSED(start)
	Q_UNUSED(end)
	DiveHandler *item = new DiveHandler();
	scene()->addItem(item);
	handles << item;

	connect(item, SIGNAL(moved()), this, SLOT(recreatePlannedDive()));
	connect(item, SIGNAL(clicked()), this, SLOT(divePlannerHandlerClicked()));
	connect(item, SIGNAL(released()), this, SLOT(divePlannerHandlerReleased()));
	QGraphicsSimpleTextItem *gasChooseBtn = new QGraphicsSimpleTextItem();
	scene()->addItem(gasChooseBtn);
	gasChooseBtn->setZValue(10);
	gasChooseBtn->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	gases << gasChooseBtn;
	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	if (plannerModel->recalcQ())
		replot();
}

void ProfileWidget2::pointsRemoved(const QModelIndex &, int start, int end)
{ // start and end are inclusive.
	int num = (end - start) + 1;
	for (int i = num; i != 0; i--) {
		delete handles.back();
		handles.pop_back();
		delete gases.back();
		gases.pop_back();
	}
	scene()->clearSelection();
	replot();
}

void ProfileWidget2::repositionDiveHandlers()
{
	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	// Re-position the user generated dive handlers
	for (int i = 0; i < plannerModel->rowCount(); i++) {
		struct divedatapoint datapoint = plannerModel->at(i);
		if (datapoint.time == 0) // those are the magic entries for tanks
			continue;
		DiveHandler *h = handles.at(i);
		h->setVisible(datapoint.entered);
		h->setPos(timeAxis->posAtValue(datapoint.time), profileYAxis->posAtValue(datapoint.depth));
		QPointF p1;
		if (i == 0) {
			if (prefs.drop_stone_mode)
				// place the text on the straight line from the drop to stone position
				p1 = QPointF(timeAxis->posAtValue(datapoint.depth / prefs.descrate),
					     profileYAxis->posAtValue(datapoint.depth));
			else
				// place the text on the straight line from the origin to the first position
				p1 = QPointF(timeAxis->posAtValue(0), profileYAxis->posAtValue(0));
		} else {
			// place the text on the line from the last position
			p1 = handles[i - 1]->pos();
		}
		QPointF p2 = handles[i]->pos();
		QLineF line(p1, p2);
		QPointF pos = line.pointAt(0.5);
		gases[i]->setPos(pos);
		gases[i]->setText(get_gas_string(displayed_dive.cylinder[datapoint.cylinderid].gasmix));
		gases[i]->setVisible(datapoint.entered &&
				(i == 0 || gases[i]->text() != gases[i-1]->text()));
	}
}

int ProfileWidget2::fixHandlerIndex(DiveHandler *activeHandler)
{
	int index = handles.indexOf(activeHandler);
	if (index > 0 && index < handles.count() - 1) {
		DiveHandler *before = handles[index - 1];
		if (before->pos().x() > activeHandler->pos().x()) {
			handles.swap(index, index - 1);
			return index - 1;
		}
		DiveHandler *after = handles[index + 1];
		if (after->pos().x() < activeHandler->pos().x()) {
			handles.swap(index, index + 1);
			return index + 1;
		}
	}
	return index;
}

void ProfileWidget2::recreatePlannedDive()
{
	DiveHandler *activeHandler = qobject_cast<DiveHandler *>(sender());
	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	int index = fixHandlerIndex(activeHandler);
	int mintime = 0, maxtime = (timeAxis->maximum() + 10) * 60;
	if (index > 0)
		mintime = plannerModel->at(index - 1).time;
	if (index < plannerModel->size() - 1)
		maxtime = plannerModel->at(index + 1).time;

	int minutes = rint(timeAxis->valueAt(activeHandler->pos()) / 60);
	if (minutes * 60 <= mintime || minutes * 60 >= maxtime)
		return;

	divedatapoint data = plannerModel->at(index);
	data.depth = rint(profileYAxis->valueAt(activeHandler->pos()) / M_OR_FT(1, 1)) * M_OR_FT(1, 1);
	data.time = rint(timeAxis->valueAt(activeHandler->pos()));

	plannerModel->editStop(index, data);
}

void ProfileWidget2::keyDownAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	Q_FOREACH (QGraphicsItem *i, scene()->selectedItems()) {
		if (DiveHandler *handler = qgraphicsitem_cast<DiveHandler *>(i)) {
			int row = handles.indexOf(handler);
			divedatapoint dp = plannerModel->at(row);
			if (dp.depth >= profileYAxis->maximum())
				continue;

			dp.depth += M_OR_FT(1, 5);
			plannerModel->editStop(row, dp);
		}
	}
}

void ProfileWidget2::keyUpAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	Q_FOREACH (QGraphicsItem *i, scene()->selectedItems()) {
		if (DiveHandler *handler = qgraphicsitem_cast<DiveHandler *>(i)) {
			int row = handles.indexOf(handler);
			divedatapoint dp = plannerModel->at(row);

			if (dp.depth <= 0)
				continue;

			dp.depth -= M_OR_FT(1, 5);
			plannerModel->editStop(row, dp);
		}
	}
}

void ProfileWidget2::keyLeftAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	Q_FOREACH (QGraphicsItem *i, scene()->selectedItems()) {
		if (DiveHandler *handler = qgraphicsitem_cast<DiveHandler *>(i)) {
			int row = handles.indexOf(handler);
			divedatapoint dp = plannerModel->at(row);

			if (dp.time / 60 <= 0)
				continue;

			// don't overlap positions.
			// maybe this is a good place for a 'goto'?
			double xpos = timeAxis->posAtValue((dp.time - 60) / 60);
			bool nextStep = false;
			Q_FOREACH (DiveHandler *h, handles) {
				if (IS_FP_SAME(h->pos().x(), xpos)) {
					nextStep = true;
					break;
				}
			}
			if (nextStep)
				continue;

			dp.time -= 60;
			plannerModel->editStop(row, dp);
		}
	}
}

void ProfileWidget2::keyRightAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	Q_FOREACH (QGraphicsItem *i, scene()->selectedItems()) {
		if (DiveHandler *handler = qgraphicsitem_cast<DiveHandler *>(i)) {
			int row = handles.indexOf(handler);
			divedatapoint dp = plannerModel->at(row);
			if (dp.time / 60.0 >= timeAxis->maximum())
				continue;

			// don't overlap positions.
			// maybe this is a good place for a 'goto'?
			double xpos = timeAxis->posAtValue((dp.time + 60) / 60);
			bool nextStep = false;
			Q_FOREACH (DiveHandler *h, handles) {
				if (IS_FP_SAME(h->pos().x(), xpos)) {
					nextStep = true;
					break;
				}
			}
			if (nextStep)
				continue;

			dp.time += 60;
			plannerModel->editStop(row, dp);
		}
	}
}

void ProfileWidget2::keyDeleteAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	int selCount = scene()->selectedItems().count();
	if (selCount) {
		QVector<int> selectedIndexes;
		Q_FOREACH (QGraphicsItem *i, scene()->selectedItems()) {
			if (DiveHandler *handler = qgraphicsitem_cast<DiveHandler *>(i)) {
				selectedIndexes.push_back(handles.indexOf(handler));
				handler->hide();
			}
		}
		plannerModel->removeSelectedPoints(selectedIndexes);
	}
}

void ProfileWidget2::keyEscAction()
{
	if (currentState != ADD && currentState != PLAN)
		return;

	if (scene()->selectedItems().count()) {
		scene()->clearSelection();
		return;
	}

	DivePlannerPointsModel *plannerModel = DivePlannerPointsModel::instance();
	if (plannerModel->isPlanner())
		plannerModel->cancelPlan();
}
#endif

void ProfileWidget2::plotPictures()
{
	Q_FOREACH (DivePictureItem *item, pictures) {
		item->hide();
		item->deleteLater();
	}
	pictures.clear();

	if (printMode)
		return;

	double x, y, lastX = -1.0, lastY = -1.0;
	DivePictureModel *m = DivePictureModel::instance();
	for (int i = 0; i < m->rowCount(); i++) {
		int offsetSeconds = m->index(i, 1).data(Qt::UserRole).value<int>();
		// it's a correct picture, but doesn't have a timestamp: only show on the widget near the
		// information area.
		if (!offsetSeconds)
			continue;
		DivePictureItem *item = new DivePictureItem();
		item->setPixmap(m->index(i, 0).data(Qt::DecorationRole).value<QPixmap>());
		item->setFileUrl(m->index(i, 1).data().toString());
		// let's put the picture at the correct time, but at a fixed "depth" on the profile
		// not sure this is ideal, but it seems to look right.
		x = timeAxis->posAtValue(offsetSeconds);
		if (i == 0)
			y = 10;
		else if (fabs(x - lastX) < 4)
			y = lastY + 3;
		else
			y = 10;
		lastX = x;
		lastY = y;
		item->setPos(x, y);
		scene()->addItem(item);
		pictures.push_back(item);
	}
}

void ProfileWidget2::dropEvent(QDropEvent *event)
{
	if (event->mimeData()->hasFormat("application/x-subsurfaceimagedrop")) {
		QByteArray itemData = event->mimeData()->data("application/x-subsurfaceimagedrop");
		QDataStream dataStream(&itemData, QIODevice::ReadOnly);

		QString filename;
		QPoint offset;
		dataStream >> filename >> offset;

		QPointF mappedPos = mapToScene(event->pos());

		FOR_EACH_PICTURE(current_dive) {
			if (QString(picture->filename) == filename) {
				picture->offset.seconds = timeAxis->valueAt(mappedPos);
				mark_divelist_changed(true);
				break;
			}
		}
		copy_dive(current_dive, &displayed_dive);
		DivePictureModel::instance()->updateDivePictures();


		if (event->source() == this) {
			event->setDropAction(Qt::MoveAction);
			event->accept();
		} else {
			event->acceptProposedAction();
		}
	} else {
		event->ignore();
	}
}

void ProfileWidget2::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat("application/x-subsurfaceimagedrop")) {
		if (event->source() == this) {
			event->setDropAction(Qt::MoveAction);
			event->accept();
		} else {
			event->acceptProposedAction();
		}
	} else {
		event->ignore();
	}
}

void ProfileWidget2::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasFormat("application/x-subsurfaceimagedrop")) {
		if (event->source() == this) {
			event->setDropAction(Qt::MoveAction);
			event->accept();
		} else {
			event->acceptProposedAction();
		}
	} else {
		event->ignore();
	}
}
