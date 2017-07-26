// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>
#include <QVariant>
#include <QGraphicsView>

class DiveCartesianAxis;

/* I currently have no idea on how to deal with this.
 */
class ColumnStatisticsWrapper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList names READ columnNames WRITE setColumnNames NOTIFY columnNamesChanged);
    Q_PROPERTY(QVariantList minValues READ minValues WRITE setMinValues NOTIFY minValuesChanged);
    Q_PROPERTY(QVariantList meanValues READ meanValues WRITE setMeanValues NOTIFY meanValuesChanged);
    Q_PROPERTY(QVariantList maxValues READ maxValues WRITE setMaxValues NOTIFY maxValuesChanged);
    Q_PROPERTY(qreal min READ min WRITE setMin NOTIFY minChanged);
    Q_PROPERTY(qreal max READ max WRITE setMax NOTIFY maxChanged);

public:
    //TODO: Export the model to be used via QML
    // Currently it's hardcoded.
    ColumnStatisticsWrapper(QObject *parent = 0);

    /* for a Column based statistics, QtCharts needs vectors,
    * where each value represents a column. */
    void setColumnNames(const QStringList& newColumnNames);
    void setMinValues(const  QVariantList& values);
    void setMeanValues(const QVariantList& values);
    void setMaxValues(const  QVariantList& values);
    void setMin(qreal min);
    void setMax(qreal max);

    QStringList columnNames() const;
    QVariantList maxValues() const;
    QVariantList minValues() const;
    QVariantList meanValues() const;
    qreal min() const;
    qreal max() const;

    virtual void repopulateData() = 0;
private:
    QStringList m_columnNames;
    QVariantList m_meanValues;
    QVariantList m_minValues;
    QVariantList m_maxValues;
    qreal m_min;
    qreal m_max;

signals:
    void columnNamesChanged(const QStringList& names);
    void minValuesChanged(const  QVariantList& minValues);
    void meanValuesChanged(const QVariantList& meanValues);
    void maxValuesChanged(const  QVariantList& maxValues);
    void minChanged(qreal min);
    void maxChanged(qreal max);
};

class TripDepthStatistics : public ColumnStatisticsWrapper {
    Q_OBJECT
public:
    TripDepthStatistics(QObject *parent = 0);
    void repopulateData() override;
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
