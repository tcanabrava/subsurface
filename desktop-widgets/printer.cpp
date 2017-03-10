#include "printer.h"
#include "templatelayout.h"
#include "core/statistics.h"
#include "core/helpers.h"

#include <algorithm>
#include <QPainter>
#ifdef USE_WEBENGINE
#include <QtWebEngineWidgets>
#else
#include <QtWebKitWidgets>
#include <QWebElementCollection>
#include <QWebElement>
#endif
#include "profile-widget/profilewidget2.h"

Printer::Printer(QPaintDevice *paintDevice, print_options *printOptions, template_options *templateOptions,  PrintMode printMode)
{
	this->paintDevice = paintDevice;
	this->printOptions = printOptions;
	this->templateOptions = templateOptions;
	this->printMode = printMode;
	dpi = 0;
	done = 0;
#ifdef USE_WEBENGINE
	webView = new QWebEngineView();
#else
	webView = new QWebView();
#endif
}

Printer::~Printer()
{
	delete webView;
}

void Printer::putProfileImage(QRect profilePlaceholder, QRect viewPort, QPainter *painter, struct dive *dive, QPointer<ProfileWidget2> profile)
{
	int x = profilePlaceholder.x() - viewPort.x();
	int y = profilePlaceholder.y() - viewPort.y();
	// use the placeHolder and the viewPort position to calculate the relative position of the dive profile.
	QRect pos(x, y, profilePlaceholder.width(), profilePlaceholder.height());
	profile->plotDive(dive, true);

	if (!printOptions->color_selected) {
		QImage image(pos.width(), pos.height(), QImage::Format_ARGB32);
		QPainter imgPainter(&image);
		imgPainter.setRenderHint(QPainter::Antialiasing);
		imgPainter.setRenderHint(QPainter::SmoothPixmapTransform);
		profile->render(&imgPainter, QRect(0, 0, pos.width(), pos.height()));
		imgPainter.end();

		// convert QImage to grayscale before rendering
		for (int i = 0; i < image.height(); i++) {
			QRgb *pixel = reinterpret_cast<QRgb *>(image.scanLine(i));
			QRgb *end = pixel + image.width();
			for (; pixel != end; pixel++) {
				int gray_val = qGray(*pixel);
				*pixel = QColor(gray_val, gray_val, gray_val).rgb();
			}
		}

		painter->drawImage(pos, image);
	} else {
		profile->render(painter, pos);
	}
}

void Printer::flowRender()
{
	// add extra padding at the bottom to pages with height not divisible by view port
#ifndef USE_WEBENGINE
	int paddingBottom = pageSize.height() - (webView->page()->mainFrame()->contentsSize().height() % pageSize.height());
	QString styleString = QString::fromUtf8("padding-bottom: ") + QString::number(paddingBottom) + "px;";
	webView->page()->mainFrame()->findFirstElement("body").setAttribute("style", styleString);

	// render the Qwebview
	QPainter painter;
	QRect viewPort(0, 0, 0, 0);
	painter.begin(paintDevice);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	// get all references to dontbreak divs
	int start = 0, end = 0;
	int fullPageResolution = webView->page()->mainFrame()->contentsSize().height();
	QWebElementCollection dontbreak = webView->page()->mainFrame()->findAllElements(".dontbreak");
	foreach (QWebElement dontbreakElement, dontbreak) {
		if ((dontbreakElement.geometry().y() + dontbreakElement.geometry().height()) - start < pageSize.height()) {
			// One more element can be placed
			end = dontbreakElement.geometry().y() + dontbreakElement.geometry().height();
		} else {
			// fill the page with background color
			QRect fullPage(0, 0, pageSize.width(), pageSize.height());
			QBrush fillBrush(templateOptions->color_palette.color1);
			painter.fillRect(fullPage, fillBrush);
			QRegion reigon(0, 0, pageSize.width(), end - start);
			viewPort.setRect(0, start, pageSize.width(), end - start);

			// render the base Html template
			webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer, reigon);

			// scroll the webview to the next page
			webView->page()->mainFrame()->scroll(0, dontbreakElement.geometry().y() - start);

			// rendering progress is 4/5 of total work
			emit(progessUpdated((end * 80.0 / fullPageResolution) + done));

			// add new pages only in print mode, while previewing we don't add new pages
			if (printMode == Printer::PRINT)
				static_cast<QPrinter*>(paintDevice)->newPage();
			else {
				painter.end();
				return;
			}
			start = dontbreakElement.geometry().y();
		}
	}
	// render the remianing page
	QRect fullPage(0, 0, pageSize.width(), pageSize.height());
	QBrush fillBrush(templateOptions->color_palette.color1);
	painter.fillRect(fullPage, fillBrush);
	QRegion reigon(0, 0, pageSize.width(), end - start);
	webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer, reigon);

	painter.end();
#else
	// FIX ME
#endif
}

void Printer::render(int Pages = 0)
{
	// keep original preferences
	QPointer<ProfileWidget2> profile = MainWindow::instance()->graphics();
	int profileFrameStyle = profile->frameStyle();
	int animationOriginal = prefs.animation_speed;
	double fontScale = profile->getFontPrintScale();
	double printFontScale = 1.0;

	// apply printing settings to profile
	profile->setFrameStyle(QFrame::NoFrame);
	profile->setPrintMode(true, !printOptions->color_selected);
	profile->setToolTipVisibile(false);
	prefs.animation_speed = 0;

	// render the Qwebview
	QPainter painter;
	QRect viewPort(0, 0, pageSize.width(), pageSize.height());
	painter.begin(paintDevice);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	// get all refereces to diveprofile class in the Html template
#ifdef USE_WEBENGINE
	//FIX ME
#else
	QWebElementCollection collection = webView->page()->mainFrame()->findAllElements(".diveprofile");

	QSize originalSize = profile->size();
	if (collection.count() > 0) {
		printFontScale = (double)collection.at(0).geometry().size().height() / (double)profile->size().height();
		profile->resize(collection.at(0).geometry().size());
	}
	profile->setFontPrintScale(printFontScale);

	int elemNo = 0;
	for (int i = 0; i < Pages; i++) {
		// render the base Html template
		webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer);

		// render all the dive profiles in the current page
		while (elemNo < collection.count() && collection.at(elemNo).geometry().y() < viewPort.y() + viewPort.height()) {
			// dive id field should be dive_{{dive_no}} se we remove the first 5 characters
			QString diveIdString = collection.at(elemNo).attribute("id");
			int diveId = diveIdString.remove(0, 5).toInt(0, 10);
			putProfileImage(collection.at(elemNo).geometry(), viewPort, &painter, get_dive_by_uniq_id(diveId), profile);
			elemNo++;
		}

		// scroll the webview to the next page
		webView->page()->mainFrame()->scroll(0, pageSize.height());
		viewPort.adjust(0, pageSize.height(), 0, pageSize.height());

		// rendering progress is 4/5 of total work
		emit(progessUpdated((i * 80.0 / Pages) + done));
		if (i < Pages - 1 && printMode == Printer::PRINT)
			static_cast<QPrinter*>(paintDevice)->newPage();
	}
	painter.end();
#endif

	// return profle settings
	profile->setFrameStyle(profileFrameStyle);
	profile->setPrintMode(false);
	profile->setFontPrintScale(fontScale);
	profile->setToolTipVisibile(true);
#ifdef USE_WEBENGINE
	//FIXME
#else
	profile->resize(originalSize);
#endif
	prefs.animation_speed = animationOriginal;

	//replot the dive after returning the settings
	profile->plotDive(0, true);
}

//value: ranges from 0 : 100 and shows the progress of the templating engine
void Printer::templateProgessUpdated(int value)
{
	done = value / 5; //template progess if 1/5 of total work
	emit progessUpdated(done);
}

void Printer::print()
{
	// we can only print if "PRINT" mode is selected
	if (printMode != Printer::PRINT) {
		return;
	}


	QPrinter *printerPtr;
	printerPtr = static_cast<QPrinter*>(paintDevice);

	TemplateLayout t(printOptions, templateOptions);
	connect(&t, SIGNAL(progressUpdated(int)), this, SLOT(templateProgessUpdated(int)));
	dpi = printerPtr->resolution();
	//rendering resolution = selected paper size in inchs * printer dpi
	pageSize.setHeight(qCeil(printerPtr->pageRect(QPrinter::Inch).height() * dpi));
	pageSize.setWidth(qCeil(printerPtr->pageRect(QPrinter::Inch).width() * dpi));
#ifdef USE_WEBENGINE
	//FIXME
#else
	webView->page()->setViewportSize(pageSize);
	webView->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical, Qt::ScrollBarAlwaysOff);
#endif
	// export border width with at least 1 pixel
	// templateOptions->borderwidth = std::max(1, pageSize.width() / 1000);
	if (printOptions->type == print_options::DIVELIST) {
		webView->setHtml(t.generate());
	} else if (printOptions->type == print_options::STATISTICS ) {
		webView->setHtml(t.generateStatistics());
	}
	if (printOptions->color_selected && printerPtr->colorMode()) {
		printerPtr->setColorMode(QPrinter::Color);
	} else {
		printerPtr->setColorMode(QPrinter::GrayScale);
	}
	// apply user settings
	int divesPerPage;

	// get number of dives per page from data-numberofdives attribute in the body of the selected template
	bool ok;
#ifdef USE_WEBENGINE
	// FIX ME
#else
	divesPerPage = webView->page()->mainFrame()->findFirstElement("body").attribute("data-numberofdives").toInt(&ok);
	if (!ok) {
		divesPerPage = 1; // print each dive in a single page if the attribute is missing or malformed
		//TODO: show warning
	}
#endif
	int Pages;
	if (divesPerPage == 0) {
		flowRender();
	} else {
		Pages = qCeil(getTotalWork(printOptions) / (float)divesPerPage);
		render(Pages);
	}
}

void Printer::previewOnePage()
{
	if (printMode == PREVIEW) {
		TemplateLayout t(printOptions, templateOptions);

		pageSize.setHeight(paintDevice->height());
		pageSize.setWidth(paintDevice->width());
#ifdef USE_WEBENGINE
		//FIXME
#else
		webView->page()->setViewportSize(pageSize);
#endif
		// initialize the border settings
		// templateOptions->border_width = std::max(1, pageSize.width() / 1000);
		if (printOptions->type == print_options::DIVELIST) {
			webView->setHtml(t.generate());
		} else if (printOptions->type == print_options::STATISTICS ) {
			webView->setHtml(t.generateStatistics());
		}
#ifdef USE_WEBENGINE
		// FIX ME
		render(1);
#else
		bool ok;
		int divesPerPage = webView->page()->mainFrame()->findFirstElement("body").attribute("data-numberofdives").toInt(&ok);
		if (!ok) {
			divesPerPage = 1; // print each dive in a single page if the attribute is missing or malformed
			//TODO: show warning
		}
		if (divesPerPage == 0) {
			flowRender();
		} else {
			render(1);
		}
#endif
	}
}
