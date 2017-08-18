// SPDX-License-Identifier: GPL-2.0
#include "TabDiveStatistics.h"

#include <tuple>

#include <core/helpers.h>
#include <core/display.h>
#include <core/statistics.h>

#include <qt-models/yearlystatisticsmodel.h>

#include <QHBoxLayout>
#include <QQuickWidget>
#include <QQmlContext>
#include <QVector>

class TripScaleDraw: public QwtScaleDraw
{
public:
    TripScaleDraw(const QStringList& tripNames) : tripNames(tripNames)
    {
        setTickLength( QwtScaleDiv::MajorTick, 0 );
        setTickLength( QwtScaleDiv::MinorTick, 0 );
        setTickLength( QwtScaleDiv::MediumTick, 0 );

        setLabelRotation( -90.0 );
        setLabelAlignment( Qt::AlignLeft | Qt::AlignVCenter );

        setSpacing( 0 );
    }

    QwtText label( double value ) const override
    {
        int pos = value;
        if (tripNames.count() <= pos)
            return QString();
        if (tripNames.at(pos).isEmpty()) {
            return QObject::tr("No trip");
        }
        return tripNames.at(pos);
    }

    QStringList tripNames;
};

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QGridLayout();

    tripSacScale = new QwtScaleWidget();
    tripDepthScale = new QwtScaleWidget();
    tripTempScale = new QwtScaleWidget();
    tripNames = new QwtScaleWidget(QwtScaleDraw::BottomScale);

    tripSacScale->setSpacing(0);
    tripDepthScale->setSpacing(0);
    tripTempScale->setSpacing(0);
    tripNames->setSpacing(0);

    tripSacPlot = new MinAvgMaxPlot(this);
    tripSacPlot->setObjectName("SacTripStatistics");
    tripSacPlot->setAxisTitle( QwtPlot::yLeft, QString( "Sac" ));
    tripSacPlot->enableAxis(QwtPlot::xBottom, false);
    tripSacPlot->enableAxis(QwtPlot::yLeft, false);

    tripTempPlot = new MinAvgMaxPlot(this);
    tripTempPlot->setObjectName("Temp Trip statistics");
    tripTempPlot->setAxisTitle( QwtPlot::yLeft, QString( "Temp" ));
    tripTempPlot->enableAxis(QwtPlot::xBottom, false);
    tripTempPlot->enableAxis(QwtPlot::yLeft, false);

    tripDepthPlot = new MinAvgMaxPlot(this);
    tripDepthPlot->setObjectName("DepthTripstatistics");
    tripDepthPlot->setAxisTitle( QwtPlot::yLeft, QString( "Depth" ));
    tripDepthPlot->enableAxis(QwtPlot::yLeft, false);
    tripDepthPlot->enableAxis(QwtPlot::xBottom, false);

    layout->addWidget(tripSacScale, 0, 0);
    layout->addWidget(tripSacPlot, 0, 1);

    layout->addWidget(tripTempScale, 1, 0);
    layout->addWidget(tripTempPlot, 1, 1);

    layout->addWidget(tripDepthScale, 2, 0);
    layout->addWidget(tripDepthPlot, 2, 1);

    layout->addWidget(tripNames, 3, 1);
    setLayout(layout);
}

TabDiveStatistics::~TabDiveStatistics()
{
}

void TabDiveStatistics::clear()
{
}

void TabDiveStatistics::updateData()
{
    QVector<MinAvgMax> depthTripValues;
    QVector<MinAvgMax> tempTripValues;
    QVector<MinAvgMax> sacTripValues;
    int biggerDepth = INT_MIN;
    int biggerTemp = INT_MIN;
    int biggerSac = INT_MIN;

    QStringList names;
    QList<double> majorTicks;

    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            majorTicks += i;
            depthTripValues.push_back({stats.min_depth.mm/1000, stats.avg_depth.mm/1000, stats.max_depth.mm/1000});
            tempTripValues.push_back({get_temp_units(stats.min_temp, nullptr), 0, get_temp_units(stats.max_temp, nullptr)});
            sacTripValues.push_back({stats.min_sac.mliter/1000, stats.avg_sac.mliter/1000, stats.max_sac.mliter/1000 });
            if (biggerDepth < stats.max_depth.mm) {
                biggerDepth = stats.max_depth.mm;
            }
            if (biggerTemp < stats.max_temp) {
                biggerTemp = stats.max_temp;
            }
            if (biggerSac < stats.max_sac.mliter) {
                biggerSac = stats.max_sac.mliter;
            }
            names.append(stats.location);
        }
    }

    tripDepthPlot->updateData(depthTripValues);
    tripTempPlot->updateData(tempTripValues);
    tripSacPlot->updateData(sacTripValues);

    tripSacScale->setScaleDiv(tripSacPlot->axisScaleDiv(QwtPlot::yLeft));
    tripDepthScale->setScaleDiv(tripDepthPlot->axisScaleDiv(QwtPlot::yLeft));
    tripTempScale->setScaleDiv(tripTempPlot->axisScaleDiv(QwtPlot::yLeft));

    tripNames->setScaleDiv(QwtScaleDiv(0, names.count(), QList<double>(), QList<double>(), majorTicks));
    tripNames->setScaleDraw(new TripScaleDraw(names));
}

MinAvgMaxPlot::MinAvgMaxPlot(QWidget *parent) : QwtPlot(parent), d_intervalCurve(nullptr), d_curve(nullptr), d_showHorizontalAxis(false)
{
    QwtPlotCanvas *canvas = new QwtPlotCanvas();
    canvas->setPalette( palette() );
    canvas->setBorderRadius( 10 );

    setCanvas( canvas );
}

void MinAvgMaxPlot::insertCurve( const QString& title,
    const QVector<QPointF>& samples, const QColor &color )
{
    delete (d_curve);
    d_curve = new QwtPlotCurve( title );
    d_curve->setRenderHint( QwtPlotItem::RenderAntialiased );
    d_curve->setStyle( QwtPlotCurve::NoCurve );
    d_curve->setLegendAttribute( QwtPlotCurve::LegendShowSymbol );

    QwtSymbol *symbol = new QwtSymbol( QwtSymbol::HLine );
    symbol->setSize( 12 );
    symbol->setPen( color );
    d_curve->setSymbol( symbol );

    d_curve->setSamples( samples );
    d_curve->attach( this );
}

void MinAvgMaxPlot::insertErrorBars(
    const QString &title,
    const QVector<QwtIntervalSample>& samples,
    const QColor &color )
{
    delete(d_intervalCurve);
    d_intervalCurve = new QwtPlotIntervalCurve( title );
    d_intervalCurve->setRenderHint( QwtPlotItem::RenderAntialiased );
    d_intervalCurve->setPen( Qt::white );

    QColor bg( color );
    bg.setAlpha( 150 );
    d_intervalCurve->setBrush( QBrush( bg ) );
    d_intervalCurve->setStyle( QwtPlotIntervalCurve::Tube );

    d_intervalCurve->setSamples( samples );
    d_intervalCurve->setStyle( QwtPlotIntervalCurve::NoCurve );

    QColor c( d_intervalCurve->brush().color().rgba() ); // skip alpha

    auto errorBar = new QwtIntervalSymbol( QwtIntervalSymbol::Box );
    errorBar->setWidth( 12 ); // should be something even
    errorBar->setPen(c);
    errorBar->setBrush(c);

    d_intervalCurve->setSymbol( errorBar );
    d_intervalCurve->setRenderHint( QwtPlotItem::RenderAntialiased, false );

    d_intervalCurve->attach( this );
}


void MinAvgMaxPlot::setShowHorizontalAxis(bool showAxis)
{
    d_showHorizontalAxis = showAxis;
}

void MinAvgMaxPlot::updateData(const QVector<MinAvgMax>& values)
{
    QVector<QPointF> averageData;
    QVector<QwtIntervalSample> rangeData;

    for(int i = 0, end = values.count(); i < end; i++) {
        auto value = values.at(i);
        averageData.push_back(QPointF( double( i ), value.avg));
        rangeData.push_back(QwtIntervalSample( double( i ), QwtInterval( value.min, value.max) ));
    }

    insertCurve     ("Average", averageData, Qt::black);
    insertErrorBars( "Range", rangeData, Qt::blue );

    replot();
}

