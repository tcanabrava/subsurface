#ifndef PRINTER_H
#define PRINTER_H

#include <QPrinter>
#ifdef USE_WEBENGINE
#include <QWebEngineView>
#else
#include <QWebView>
#endif
#include <QRect>
#include <QPainter>

#include "printoptions.h"
#include "templateedit.h"

class Printer : public QObject {
	Q_OBJECT

public:
	enum PrintMode {
		PRINT,
		PREVIEW
	};

private:
	QPaintDevice *paintDevice;
#ifdef USE_WEBENGINE
	QWebEngineView *webView;
#else
	QWebView *webView;
#endif
	print_options *printOptions;
	template_options *templateOptions;
	QSize pageSize;
	PrintMode printMode;
	int done;
	int dpi;
	void render(int Pages);
	void flowRender();
	void putProfileImage(QRect box, QRect viewPort, QPainter *painter, struct dive *dive, QPointer<ProfileWidget2> profile);

private slots:
	void templateProgessUpdated(int value);

public:
	Printer(QPaintDevice *paintDevice, print_options *printOptions, template_options *templateOptions, PrintMode printMode);
	~Printer();
	void print();
	void previewOnePage();

signals:
	void progessUpdated(int value);
};

#endif //PRINTER_H
