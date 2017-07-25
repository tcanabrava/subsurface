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
    qreal currMax = INT_MIN;
    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            qreal max = stats.max_depth.mm/1000.0;
            if (max > currMax) {
                currMax =  max;
            }

            maxValues.append(QVariant::fromValue(max));
            meanValues.append(QVariant::fromValue(stats.avg_depth.mm/1000.0));
            minValues.append(QVariant::fromValue(stats.min_depth.mm/1000.0));
            columnNames.append(QString(stats.location));
        }
    }

    setColumnNames(columnNames);
    setMinValues(minValues);
    setMeanValues(meanValues);
    setMaxValues(maxValues);
    setMin(0);
    setMax(currMax);
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
