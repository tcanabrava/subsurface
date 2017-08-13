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

struct MinAvgMax {
    int min;
    int avg;
    int max;
    QString info;
};

class Grid: public QwtPlotGrid
{
public:
    Grid()
    {
        enableXMin( true );
        setMajorPen( Qt::white, 0, Qt::DotLine );
        setMinorPen( Qt::gray, 0, Qt::DotLine );
    }

    virtual void updateScaleDiv( const QwtScaleDiv &xScaleDiv,
        const QwtScaleDiv &yScaleDiv )
    {
        QwtScaleDiv scaleDiv( xScaleDiv.lowerBound(), xScaleDiv.upperBound() );
        scaleDiv.setTicks( QwtScaleDiv::MinorTick, xScaleDiv.ticks( QwtScaleDiv::MinorTick ) );
        scaleDiv.setTicks( QwtScaleDiv::MajorTick, xScaleDiv.ticks( QwtScaleDiv::MediumTick ) );
        QwtPlotGrid::updateScaleDiv( scaleDiv, yScaleDiv );
    }
};

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

    virtual QwtText label( double value ) const
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

void DepthQwtPlot::updateData()
{
    QVector<MinAvgMax> values;

    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            auto min = stats.min_depth.mm;
            auto mean = stats.avg_depth.mm;
            auto max = stats.max_depth.mm;
            values.push_back({min, mean, max, QString(stats.location)});
        }
    }

    qSort(values.begin(), values.end(), [](const MinAvgMax& a, const MinAvgMax& b) {
        return a.max < b.max;
    });

    QVector<QPointF> averageData;
    QVector<QwtIntervalSample> rangeData;
    QStringList names;
    QList<double> majorTicks;

    for(int i = 0, end = values.count(); i < end; i++) {
        auto value = values.at(i);
        averageData.push_back(QPointF( double( i ), value.avg));
        rangeData.push_back(QwtIntervalSample( double( i ), QwtInterval( value.min, value.max) ));
        names.push_back(value.info);
        majorTicks += i;
    }

    insertCurve     ("Average", averageData, Qt::black);
    insertErrorBars( "Range", rangeData, Qt::blue );
    if (names.count()) {
        setAxisScaleDiv( QwtPlot::xBottom, QwtScaleDiv(0, names.count(), QList<double>(), QList<double>(), majorTicks));
        setAxisScaleDraw( QwtPlot::xBottom, new TripScaleDraw(names) );
    }
    replot();
}

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    tripDepthPlot = new DepthQwtPlot();

    layout->addWidget(tripDepthPlot);
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
    tripDepthPlot->updateData();
}

DepthQwtPlot::DepthQwtPlot() : d_intervalCurve(nullptr), d_curve(nullptr)
{
    setObjectName("DepthStatistics");
    setTitle("Depths / trips");
    setAxisTitle( QwtPlot::xBottom, "Trips" );
    setAxisTitle( QwtPlot::yLeft, QString( "Depth / Meters" ));

    QwtPlotCanvas *canvas = new QwtPlotCanvas();
    canvas->setPalette( palette() );
    canvas->setBorderRadius( 10 );

    insertLegend( new QwtLegend(), QwtPlot::RightLegend );

    setCanvas( canvas );

    Grid* g = new Grid();
    g->attach(this);
}

void DepthQwtPlot::insertCurve( const QString& title,
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

void DepthQwtPlot::insertErrorBars(
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

    QwtIntervalSymbol *errorBar =
        new QwtIntervalSymbol( QwtIntervalSymbol::Box );
    errorBar->setWidth( 12 ); // should be something even
    errorBar->setPen(c);
    errorBar->setBrush(c);
    d_intervalCurve->setSymbol( errorBar );
    d_intervalCurve->setRenderHint( QwtPlotItem::RenderAntialiased, false );

    d_intervalCurve->attach( this );
}

