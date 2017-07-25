// SPDX-License-Identifier: GPL-2.0
#include "TabDiveStatistics.h"

#include <core/helpers.h>
#include <core/display.h>
#include <core/statistics.h>

#include <QHBoxLayout>
#include <QQuickWidget>
#include <QQmlContext>

QList<QString> ColumnStatisticsWrapper::columnNames()
{
    return m_columnNames;
}

QList<int> ColumnStatisticsWrapper::maxValues()
{
    return m_maxValues;
}

QList<int> ColumnStatisticsWrapper::meanValues()
{
    return m_meanValues;
}

QList<int> ColumnStatisticsWrapper::minValues()
{
    return m_minValues;
}

void ColumnStatisticsWrapper::setColumnNames(const QList<QString>& newColumnNames)
{
    m_columnNames = newColumnNames;
}

void ColumnStatisticsWrapper::setMaxValues(const QList<int>& maxValues)
{
    m_maxValues = maxValues;
}

void ColumnStatisticsWrapper::setMeanValues(const QList<int>& meanValues)
{
    m_meanValues = meanValues;
}

void ColumnStatisticsWrapper::setMinValues(const QList<int>& minValues)
{
    m_minValues = minValues;
}

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    auto quickWidget = new QQuickWidget();

    m_columnsDepth = new ColumnStatisticsWrapper();

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

    m_columnsDepth->setColumnNames(columnNames);
    m_columnsDepth->setMinValues(minValues);
    m_columnsDepth->setMeanValues(meanValues);
    m_columnsDepth->setMaxValues(maxValues);
    m_columnsDepth->changed();
}

