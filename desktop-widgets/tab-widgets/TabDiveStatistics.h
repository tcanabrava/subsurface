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
#include <qwt_scale_widget.h>

class QwtPlotIntervalCurve;
class QwtPlotCurve;

struct MinAvgMax {
    int min;
    int avg;
    int max;
    QString info;
};

class MinAvgMaxPlot : public QwtPlot {
    Q_OBJECT
public:
    MinAvgMaxPlot(QWidget *parent = 0);
    void updateData(const QVector<MinAvgMax>& data);

    void insertErrorBars(const QString &title, const QVector<QwtIntervalSample>& samples, const QColor &color );
    void insertCurve( const QString& title, const QVector<QPointF>& samples, const QColor &color );

    /* call this function before calling updateData() */
    void setShowHorizontalAxis(bool showAxis);
private:
    QwtPlotIntervalCurve *d_intervalCurve;
    QwtPlotCurve *d_curve;
    bool d_showHorizontalAxis;
};

class TabDiveStatistics : public TabBase {
	Q_OBJECT
public:
	TabDiveStatistics(QWidget *parent = 0);
	~TabDiveStatistics();
	void updateData() override;
	void clear() override;
private:
    QwtScaleWidget *tripSacScale;
    MinAvgMaxPlot *tripSacPlot;
    QwtScaleWidget *tripTempScale;
    MinAvgMaxPlot *tripTempPlot;
    QwtScaleWidget *tripDepthScale;
    MinAvgMaxPlot *tripDepthPlot;
};

#endif
