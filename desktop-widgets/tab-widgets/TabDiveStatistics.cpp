// SPDX-License-Identifier: GPL-2.0
#include "TabDiveStatistics.h"

#include <tuple>

#include <core/helpers.h>
#include <core/display.h>
#include <core/statistics.h>

#include <profile-widget/divecartesianaxis.h>

#include <qt-models/yearlystatisticsmodel.h>

#include <QHBoxLayout>
#include <QQuickWidget>
#include <QQmlContext>

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

DiveStatisticsView::DiveStatisticsView(QWidget* parent) : QGraphicsView(parent)
{
    setupSceneAndFlags();

    depthYAxis = new DiveCartesianAxis();
    depthYAxis->setOrientation(DiveCartesianAxis::TopToBottom);
    depthYAxis->setMinimum(0);
    depthYAxis->setMaximum(64);
    depthYAxis->setTickInterval(5);
    depthYAxis->setTickSize(0.5);
    depthYAxis->setLineSize(96);
    depthYAxis->setPos(3,3);
    depthYAxis->setVisible(true);
    depthYAxis->setLine(QLineF(0,0,0,90));

    depthXAxis = new DiveCartesianAxis();
    depthXAxis->setMinimum(0);
    depthXAxis->setMaximum(11);
    depthXAxis->setTickInterval(1);
    depthXAxis->setTickSize(0.5);
    depthXAxis->setLineSize(96);
    depthXAxis->setPos(3,95);
    depthXAxis->setVisible(true);
    depthXAxis->setLine(QLine(0,0,90,0));

    scene()->addItem(depthYAxis);
    scene()->addItem(depthXAxis);
}

void DiveStatisticsView::setupSceneAndFlags()
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
    setBackgroundBrush(getColor(::BACKGROUND));
}

void DiveStatisticsView::refreshContents()
{
    depthYAxis->updateTicks();
    depthXAxis->updateTicks();
}

void DiveStatisticsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    fitInView(sceneRect(), Qt::IgnoreAspectRatio);
}

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent),
 m_statisticsView(new DiveStatisticsView(this))
{
    auto layout = new QHBoxLayout();

    /* TODO: Maybe restore this later, for now it's broken.
    auto quickWidget = new QQuickWidget();
    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quickWidget->rootContext()->setContextProperty("columnsDepthStatistics", m_columnsDepth);
    quickWidget->setSource(QUrl::fromLocalFile(":/qml/statistics.qml"));
    layout->addWidget(quickWidget);
    */
    layout->addWidget(m_statisticsView);
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
    m_statisticsView->refreshContents();
}
