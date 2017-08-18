// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>
#include <QVariant>
#include <QColor>
#include <QVector>

#include <qwt_interval.h>
#include <qwt_interval_symbol.h>
#include <qwt_legend.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_samples.h>
#include <qwt_symbol.h>
#include <qwt_scale_draw.h>

class QwtPlotIntervalCurve;
class QwtPlotCurve;

class MinAvgMaxPlot : public QwtPlot {
    Q_OBJECT
public:
    MinAvgMaxPlot(QWidget *parent = 0);
    virtual void updateData() = 0;

    void insertErrorBars(const QString &title, const QVector<QwtIntervalSample>& samples, const QColor &color );
    void insertCurve( const QString& title, const QVector<QPointF>& samples, const QColor &color );

private:
    QwtPlotIntervalCurve *d_intervalCurve;
    QwtPlotCurve *d_curve;
};

class TripDepthPlot : public MinAvgMaxPlot {
    Q_OBJECT
public:
    TripDepthPlot(QWidget *parent = 0);
    void updateData() override;
};

class TabDiveStatistics : public TabBase {
	Q_OBJECT
public:
	TabDiveStatistics(QWidget *parent = 0);
	~TabDiveStatistics();
	void updateData() override;
	void clear() override;
private:
    TripDepthPlot *tripDepthPlot;
};

#endif
