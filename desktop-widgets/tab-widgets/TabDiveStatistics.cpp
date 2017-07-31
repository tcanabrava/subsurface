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

ColumnStatisticsWrapper::ColumnStatisticsWrapper(QObject* parent) : QObject(parent)
{
}

QStringList ColumnStatisticsWrapper::columnNames() const
{
    return m_columnNames;
}

QVariantList ColumnStatisticsWrapper::maxValues() const
{
    return m_maxValues;
}

QVariantList ColumnStatisticsWrapper::meanValues() const
{
    return m_meanValues;
}

QVariantList ColumnStatisticsWrapper::minValues() const
{
    return m_minValues;
}

void ColumnStatisticsWrapper::setColumnNames(const QStringList& newColumnNames)
{
    m_columnNames = newColumnNames;
    emit columnNamesChanged(m_columnNames);

}

void ColumnStatisticsWrapper::setMaxValues(const QVariantList& values)
{
    m_maxValues = values;
    emit maxValuesChanged(m_maxValues);
}

void ColumnStatisticsWrapper::setMeanValues(const QVariantList& values)
{
    m_meanValues = values;
    emit meanValuesChanged(m_meanValues);
}

void ColumnStatisticsWrapper::setMinValues(const QVariantList& values)
{
    m_minValues = values;
    emit minValuesChanged(m_minValues);
}

qreal ColumnStatisticsWrapper::max() const
{
    return m_max;
}

qreal ColumnStatisticsWrapper::min() const
{
    return m_min;
}

void ColumnStatisticsWrapper::setMax(qreal max)
{
    m_max = max;
    emit maxChanged(m_max);
}

void ColumnStatisticsWrapper::setMin(qreal min)
{
    m_min = min;
    emit minChanged(m_min);
}


TripDepthStatistics::TripDepthStatistics(QObject* parent) : ColumnStatisticsWrapper(parent)
{
}

void TripDepthStatistics::repopulateData()
{
    QList<QString> columnNames;
    QVariantList minValues;
    QVariantList meanValues;
    QVariantList maxValues;
    typedef std::tuple<int,int,int,QString> tripStat;
    std::vector<tripStat> values;

    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            auto min = stats.min_depth.mm;
            auto mean = stats.avg_depth.mm;
            auto max = stats.max_depth.mm;

            values.push_back({min, mean, max, QString(stats.location)});
        }
    }

    qSort(values.begin(), values.end(), [](const tripStat& a, const tripStat& b) {
        return std::get<2>(a) < std::get<2>(b);
    });

    for(const auto& trip : values) {
        minValues.push_back(std::get<0>(trip)  / 1000.0);
        meanValues.push_back(std::get<1>(trip) / 1000.0);
        maxValues.push_back(std::get<2>(trip)  / 1000.0);
        columnNames.push_back(std::get<3>(trip));
    }

    setColumnNames(columnNames);
    setMinValues(minValues);
    setMeanValues(meanValues);
    setMaxValues(maxValues);
    setMin(0);
    setMax(maxValues.count() ? maxValues.last().toInt() : 0);
}

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

        setLabelRotation( -60.0 );
        setLabelAlignment( Qt::AlignLeft | Qt::AlignVCenter );

        setSpacing( 0 );
    }

    virtual QwtText label( double value ) const
    {
        qDebug() << "value" << value;
        int pos = value;
        if (tripNames.count() <= pos)
            return QString();
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

    /* TODO: Maybe restore this later, for now it's broken.
    auto quickWidget = new QQuickWidget();
    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quickWidget->rootContext()->setContextProperty("columnsDepthStatistics", m_columnsDepth);
    quickWidget->setSource(QUrl::fromLocalFile(":/qml/statistics.qml"));
    layout->addWidget(quickWidget);
    */
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
    canvas->setPalette( Qt::darkGray );
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

    QwtSymbol *symbol = new QwtSymbol( QwtSymbol::XCross );
    symbol->setSize( 4 );
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

    QColor c( d_intervalCurve->brush().color().rgb() ); // skip alpha

    QwtIntervalSymbol *errorBar =
        new QwtIntervalSymbol( QwtIntervalSymbol::Box );
    errorBar->setWidth( 8 ); // should be something even
    errorBar->setPen( c );

    d_intervalCurve->setSymbol( errorBar );
    d_intervalCurve->setRenderHint( QwtPlotItem::RenderAntialiased, false );

    d_intervalCurve->attach( this );
}

