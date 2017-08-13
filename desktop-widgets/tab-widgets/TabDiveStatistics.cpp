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

MinAvgMaxModel::MinAvgMaxModel(QObject *parent) : QAbstractTableModel(parent)
{

}

MinAvgMaxModel::~MinAvgMaxModel()
{

}

int MinAvgMaxModel::columnCount(const QModelIndex& parent) const {
    return 5;
}

int MinAvgMaxModel::rowCount(const QModelIndex& parent) const {
    return internalData.count();
}

QVariant MinAvgMaxModel::data(const QModelIndex& index, int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch(index.column()) {
        case 0 : Q_FALLTHROUGH();
        case 1 : return internalData.at(index.row()).min;
        case 2 : return internalData.at(index.row()).avg;
        case 3 : Q_FALLTHROUGH();
        case 4 : return internalData.at(index.row()).max;
    }
    return {};
}

QVariant MinAvgMaxModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return rowHeaders.at(section);
    }
    return {};
}


TripDepthModel::TripDepthModel(QObject* parent) : MinAvgMaxModel(parent)
{
}

void TripDepthModel::repopulateData()
{
    beginResetModel();
    internalData.clear();
    rowHeaders.clear();
    MinAvgMax currData;

    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            currData.min = stats.min_depth.mm/1000;
            currData.avg = stats.avg_depth.mm/1000;
            currData.max = stats.max_depth.mm/1000;
            rowHeaders.append(QString(stats.location));
            internalData.append(currData);
        }
    }

    endResetModel();
}

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    auto quickWidget = new QQuickWidget();

    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);

    quickWidget->rootContext()->setContextProperty("columnsDepthStatistics", nullptr);
    quickWidget->setSource(QUrl::fromLocalFile(":/qml/statistics.qml"));

    layout->addWidget(quickWidget);
}

TabDiveStatistics::~TabDiveStatistics()
{
}

void TabDiveStatistics::clear()
{
}

void TabDiveStatistics::updateData()
{
}
