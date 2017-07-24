// SPDX-License-Identifier: GPL-2.0
#include "TabDiveStatistics.h"

#include <core/helpers.h>
#include <core/display.h>
#include <core/statistics.h>

#include <QHBoxLayout>
#include <QQuickWidget>

TabDiveStatistics::TabDiveStatistics(QWidget *parent) : TabBase(parent)
{
    auto layout = new QHBoxLayout();
    auto quickWidget = new QQuickWidget();

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
}

