// SPDX-License-Identifier: GPL-2.0
#include "TabDiveStatistics.h"

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

QList<QString> ColumnStatisticsWrapper::columnNames() const
{
    return m_columnNames;
}

QList<int> ColumnStatisticsWrapper::maxValues() const
{
    return m_maxValues;
}

QList< int > ColumnStatisticsWrapper::meanValues() const
{
    return m_meanValues;
}

QList<int> ColumnStatisticsWrapper::minValues() const
{
    return m_minValues;
}

void ColumnStatisticsWrapper::setColumnNames(const QList<QString>& newColumnNames)
{
    m_columnNames = newColumnNames;
    emit columnNamesChanged(m_columnNames);
}

void ColumnStatisticsWrapper::setMaxValues(const QList<int>& values)
{
    m_maxValues = values;
    emit maxValuesChanged(m_maxValues);
}

void ColumnStatisticsWrapper::setMeanValues(const QList<int>& values)
{
    m_meanValues = values;
    emit meanValuesChanged(m_meanValues);
}

void ColumnStatisticsWrapper::setMinValues(const QList<int>& values)
{
    m_minValues = values;
    emit minValuesChanged(m_minValues);
}

TripDepthStatistics::TripDepthStatistics(QObject* parent) : ColumnStatisticsWrapper(parent)
{
}

void TripDepthStatistics::repopulateData()
{
    QList<QString> columnNames;
    QList<int> minValues;
    QList<int> meanValues;
    QList<int> maxValues;

    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            minValues.append(stats.min_depth.mm/1000);
            maxValues.append(stats.max_depth.mm/1000);
            meanValues.append(stats.avg_depth.mm/1000);
            columnNames.append(QString(stats.location));
		}
    }

    setColumnNames(columnNames);
    setMinValues(minValues);
    setMeanValues(meanValues);
    setMaxValues(maxValues);
}

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    auto quickWidget = new QQuickWidget();

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
