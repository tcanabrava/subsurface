// SPDX-License-Identifier: GPL-2.0
#ifndef DIVECARTESIANAXIS_H
#define DIVECARTESIANAXIS_H

#include <QObject>
#include <QGraphicsLineItem>
#include "core/color.h"
#include "profilewidget2.h"

class QPropertyAnimation;
class DiveTextItem;
class DiveLineItem;
class DivePlotDataModel;

class DiveCartesianAxis : public QObject, public QGraphicsLineItem {
	Q_OBJECT
	Q_PROPERTY(QLineF line WRITE setLine READ line)
	Q_PROPERTY(QPointF pos WRITE setPos READ pos)
	Q_PROPERTY(qreal x WRITE setX READ x)
	Q_PROPERTY(qreal y WRITE setY READ y)
private:
	bool printMode;
	QPen gridPen();
public:
	enum Orientation {
		TopToBottom,
		BottomToTop,
		LeftToRight,
		RightToLeft
	};
	DiveCartesianAxis(ProfileWidget2 *widget = nullptr);
	virtual ~DiveCartesianAxis();
	void setPrintMode(bool mode);
	void setMinimum(double minimum);
	void setMaximum(double maximum);
	void setTickInterval(double interval);
	void setOrientation(Orientation orientation);
	void setTickSize(qreal size);
	void setFontLabelScale(qreal scale);
	double minimum() const;
	double maximum() const;
	double tickInterval() const;
	double tickSize() const;
	double fontLabelScale() const;
	qreal valueAt(const QPointF &p) const;
	qreal percentAt(const QPointF &p);
	qreal posAtValue(qreal value);
	void setColor(const QColor &color);
	void setTextColor(const QColor &color);
	void animateChangeLine(const QLineF &newLine);
	void setTextVisible(bool arg1);
	void setLinesVisible(bool arg1);
	void setLineSize(qreal lineSize);
	void setLine(const QLineF& line);
	int unitSystem;
public
slots:
	virtual void updateTicks(color_indice_t color = TIME_GRID);

signals:
	void sizeChanged();
	void maxChanged();

protected:
	ProfileWidget2 *profileWidget;
	virtual QString textForValue(double value);
	virtual QColor colorForValue(double value);
	Orientation orientation;
	QList<DiveTextItem *> labels;
	QList<DiveLineItem *> lines;
	double min;
	double max;
	double interval;
	double tick_size;
	QColor textColor;
	bool textVisibility;
	bool lineVisibility;
	double labelScale;
	qreal line_size;
	bool changed;
};

class DepthAxis : public DiveCartesianAxis {
	Q_OBJECT
public:
	DepthAxis(ProfileWidget2 *widget);
protected:
	QString textForValue(double value);
	QColor colorForValue(double value);
private
slots:
	void settingsChanged();
};

class TimeAxis : public DiveCartesianAxis {
	Q_OBJECT
public:
	TimeAxis(ProfileWidget2 *widget);
	void updateTicks(color_indice_t color = TIME_GRID);

protected:
	QString textForValue(double value);
	QColor colorForValue(double value);
};

class TemperatureAxis : public DiveCartesianAxis {
	Q_OBJECT
public:
	TemperatureAxis(ProfileWidget2 *widget);
protected:
	QString textForValue(double value);
};

class PartialGasPressureAxis : public DiveCartesianAxis {
	Q_OBJECT
public:
	PartialGasPressureAxis(ProfileWidget2 *widget);
	void setModel(DivePlotDataModel *model);
public
slots:
	void settingsChanged();

private:
	DivePlotDataModel *model;
};

#endif // DIVECARTESIANAXIS_H
