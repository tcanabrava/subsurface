// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>
#include <QVariant>
#include <QGraphicsView>

#include <profile-widget/diveprofileitem.h>

class DiveCartesianAxis;
class YearlyStatisticsModel;

struct MinimalStatistics {
    qreal min;
    qreal avg;
    qreal max;
    QString info;
};

class MinAvgMaxModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column{IDX, MIN,AVG,MAX, INFO, COLUMNS};
    MinAvgMaxModel(QObject *parent = 0);
    void sortBy(Column column);
    QVariant data(const QModelIndex & index, int role) const override;
    void setStatisticsData(const QVector<MinimalStatistics>& values);
    int columnCount(const QModelIndex & parent = QModelIndex()) const override;
    int rowCount(const QModelIndex & parent = QModelIndex()) const override;

private:
    QVector<MinimalStatistics> m_values;
};

class StatisticsItem : public AbstractProfilePolygonItem {
	Q_OBJECT
public:
	StatisticsItem();
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

class DiveStatisticsView : public QGraphicsView {
    Q_OBJECT
public:
    DiveStatisticsView(QWidget *parent);
    void refreshContents();
protected:
    void resizeEvent(QResizeEvent * event) override;
private:
    void setupSceneAndFlags();
    DiveCartesianAxis *depthYAxis;
    DiveCartesianAxis *depthXAxis;

    MinAvgMaxModel *depthModel;
    StatisticsItem *depthMinCurve;
    StatisticsItem *depthAvgCurve;
    StatisticsItem *depthMaxCurve;

    // *this is the model that has all of the Statistics
    // information. I need to create smaller models containing
    // only the information I want so I can display it.
};

class TabDiveStatistics : public TabBase {
	Q_OBJECT
public:
	TabDiveStatistics(QWidget *parent = 0);
	~TabDiveStatistics();
	void updateData() override;
	void clear() override;
private:
    DiveStatisticsView *m_statisticsView;

};

#endif
