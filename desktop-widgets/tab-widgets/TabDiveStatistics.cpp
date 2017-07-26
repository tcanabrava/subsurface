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

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    auto quickWidget = new QQuickWidget();

    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_columnsDepth = new TripDepthStatistics(this);

    quickWidget->rootContext()->setContextProperty("columnsDepthStatistics", m_columnsDepth);
    quickWidget->setSource(QUrl::fromLocalFile(":/qml/statistics.qml"));

    layout->addWidget(quickWidget);
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
    m_columnsDepth->repopulateData();
}
