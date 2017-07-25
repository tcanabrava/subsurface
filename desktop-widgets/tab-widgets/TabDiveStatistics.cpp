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
    m_columnsDepth->changed();
}

