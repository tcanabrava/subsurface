// SPDX-License-Identifier: GPL-2.0
/* qt-gui.cpp */
/* Qt UI implementation */
#include "core/dive.h"
#include "core/display.h"
#include "core/helpers.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QNetworkProxy>
#include <QLibraryInfo>

#include "core/qt-gui.h"

#include <QQuickWindow>
#include <QScreen>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSortFilterProxyModel>
#include "mobile-widgets/qmlmanager.h"
#include "qt-models/divelistmodel.h"
#include "qt-models/gpslistmodel.h"
#include "mobile-widgets/qmlprofile.h"
#include "core/downloadfromdcthread.h"

#include "mobile-widgets/qml/kirigami/src/kirigamiplugin.h"

QObject *qqWindowObject = NULL;

void init_ui()
{
	init_qt_late();
}

void run_ui()
{
	qmlRegisterType<QMLManager>("org.subsurfacedivelog.mobile", 1, 0, "QMLManager");
	qmlRegisterType<QMLProfile>("org.subsurfacedivelog.mobile", 1, 0, "QMLProfile");

	qmlRegisterType<DCDeviceData>("org.subsurfacedivelog.mobile", 1, 0, "DCDeviceData");
	qmlRegisterType<DownloadThread>("org.subsurfacedivelog.mobile", 1, 0, "DCDownloadThread");

	QQmlApplicationEngine engine;
	KirigamiPlugin::getInstance().registerTypes();
#if __APPLE__
	// when running the QML UI on a Mac the deployment of the QML Components seems
	// to fail and the search path for the components is rather odd - simply the
	// same directory the executable was started from <bundle>/Contents/MacOS/
	// To work around this we need to manually copy the components at install time
	// to Contents/Frameworks/qml and make sure that we add the correct import path
	QStringList importPathList = engine.importPathList();
	Q_FOREACH(QString importPath, importPathList) {
		if (importPath.contains("MacOS"))
			engine.addImportPath(importPath.replace("MacOS", "Frameworks"));
	}
	qDebug() << "QML import path" << engine.importPathList();
#endif
	engine.addImportPath("qrc://imports");
	DiveListModel diveListModel;
	DiveListSortModel *sortModel = new DiveListSortModel(0);
	sortModel->setSourceModel(&diveListModel);
	sortModel->setDynamicSortFilter(true);
	sortModel->setSortRole(DiveListModel::DiveDateRole);
	sortModel->sort(0, Qt::DescendingOrder);
	GpsListModel gpsListModel;
	QSortFilterProxyModel *gpsSortModel = new QSortFilterProxyModel(0);
	gpsSortModel->setSourceModel(&gpsListModel);
	gpsSortModel->setDynamicSortFilter(true);
	gpsSortModel->setSortRole(GpsListModel::GpsWhenRole);
	gpsSortModel->sort(0, Qt::DescendingOrder);
	QQmlContext *ctxt = engine.rootContext();
	ctxt->setContextProperty("diveModel", sortModel);
	ctxt->setContextProperty("gpsModel", gpsSortModel);
	ctxt->setContextProperty("vendorList", vendorList);

	engine.load(QUrl(QStringLiteral("qrc:///qml/main.qml")));
	qqWindowObject = engine.rootObjects().value(0);
	if (!qqWindowObject) {
		fprintf(stderr, "can't create window object\n");
		exit(1);
	}
	QQuickWindow *qml_window = qobject_cast<QQuickWindow *>(qqWindowObject);
	qml_window->setIcon(QIcon(":/subsurface-mobile-icon"));
	qqWindowObject->setProperty("messageText", QVariant("Subsurface-mobile startup"));
	qDebug() << "qqwindow devicePixelRatio" << qml_window->devicePixelRatio() << qml_window->screen()->devicePixelRatio();
	QScreen *screen = qml_window->screen();
	QObject::connect(qml_window, &QQuickWindow::screenChanged, QMLManager::instance(), &QMLManager::screenChanged);
	QMLManager *manager = QMLManager::instance();
	manager->setDevicePixelRatio(qml_window->devicePixelRatio(), qml_window->screen());
	manager->dlSortModel = sortModel;
	manager->screenChanged(screen);
	qDebug() << "qqwindow screen has ldpi/pdpi" << screen->logicalDotsPerInch() << screen->physicalDotsPerInch();
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
	qml_window->setHeight(1200);
	qml_window->setWidth(800);
#endif
	qml_window->show();
	qApp->exec();
}

void exit_ui()
{
	delete qApp;
	free((void *)existing_filename);
}

double get_screen_dpi()
{
	QDesktopWidget *mydesk = qApp->desktop();
	return mydesk->physicalDpiX();
}

bool haveFilesOnCommandLine()
{
	return false;
}
