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

DiveStatisticsView::DiveStatisticsView(QWidget* parent) : QGraphicsView(parent)
{
    setupSceneAndFlags();

    depthModel = new MinAvgMaxModel();
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

    depthMinCurve = new StatisticsItem();
    depthMaxCurve = new StatisticsItem();
    depthAvgCurve = new StatisticsItem();

    // Setup Models
    depthMinCurve->setObjectName("WEE");
    depthMinCurve->setModel(depthModel);
    depthMinCurve->setVerticalDataColumn(MinAvgMaxModel::MIN);
    depthMinCurve->setHorizontalDataColumn(MinAvgMaxModel::IDX);

    depthAvgCurve->setModel(depthModel);
    depthAvgCurve->setVerticalDataColumn(MinAvgMaxModel::AVG);
    depthAvgCurve->setHorizontalDataColumn(MinAvgMaxModel::IDX);

    depthMaxCurve->setModel(depthModel);
    depthMaxCurve->setVerticalDataColumn(MinAvgMaxModel::MAX);
    depthMaxCurve->setHorizontalDataColumn(MinAvgMaxModel::IDX);

    depthMinCurve->setHorizontalAxis(depthXAxis);
    depthMinCurve->setVerticalAxis(depthYAxis);

    depthAvgCurve->setHorizontalAxis(depthXAxis);
    depthAvgCurve->setVerticalAxis(depthYAxis);

    depthMaxCurve->setHorizontalAxis(depthXAxis);
    depthMaxCurve->setVerticalAxis(depthYAxis);

    depthMinCurve->setZValue(10);
    depthAvgCurve->setZValue(5);
    depthMaxCurve->setZValue(1);

    depthMinCurve->setVisible(true);
    depthMaxCurve->setVisible(true);
    depthAvgCurve->setVisible(true);
    scene()->addItem(depthMinCurve);
    scene()->addItem(depthAvgCurve);
    scene()->addItem(depthMaxCurve);
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
    qreal biggerMax = 0;
    QVector<MinimalStatistics> values;
    if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
        for (int i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
            auto stats = stats_by_trip[i];
            qreal min = stats.min_depth.mm / 1000.0;
            qreal mean = stats.avg_depth.mm / 1000.0;
            qreal max = stats.max_depth.mm / 1000.0;
            biggerMax = qMax(biggerMax, max);
            values.push_back({min, mean, max, QString(stats.location)});
        }
    }

    depthXAxis->setMaximum(values.count());
    depthYAxis->setMaximum(biggerMax);

    depthModel->setStatisticsData(values);

    depthYAxis->updateTicks();
    depthXAxis->updateTicks();
}

void DiveStatisticsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    fitInView(sceneRect(), Qt::IgnoreAspectRatio);
}

#include <QColor>

StatisticsItem::StatisticsItem()
{
    setBrush(QBrush(QColor(0,0,0)));
}

void StatisticsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(widget);
	if (polygon().isEmpty())
		return;

	painter->save();
	// This paints the Polygon + Background. I'm setting the pen to QPen() so we don't get a black line here,
	// after all we need to plot the correct velocities colors later.
	setPen(Qt::NoPen);
	QGraphicsPolygonItem::paint(painter, option, widget);
    painter->restore();
}

MinAvgMaxModel::MinAvgMaxModel(QObject* parent)
{
}

int MinAvgMaxModel::columnCount(const QModelIndex& parent) const
{
    return COLUMNS;
}

int MinAvgMaxModel::rowCount(const QModelIndex& parent) const
{
    return m_values.count();
}

void MinAvgMaxModel::setStatisticsData(const QVector<MinimalStatistics>& values)
{
    beginResetModel();
    m_values = values;
    qDebug() << "Model Changed!" << m_values.size();
    endResetModel();
}

void MinAvgMaxModel::sortBy(MinAvgMaxModel::Column column)
{
    Q_UNUSED(column);
}

QVariant MinAvgMaxModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    if(role != Qt::DisplayRole) {
        return {};
    }

    switch(index.column()) {
        case IDX : return index.row();
        case MIN : return m_values.at(index.row()).min;
        case AVG : return m_values.at(index.row()).avg;
        case MAX : return m_values.at(index.row()).max;
        case INFO: return m_values.at(index.row()).info;
    }
    return {};
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
