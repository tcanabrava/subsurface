// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>

/* I currently have no idea on how to deal with this.
 */
class ColumnStatisticsWrapper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QString> names READ columnNames WRITE setColumnNames NOTIFY columnNamesChanged);
    Q_PROPERTY(QList<int> meanValues READ meanValues WRITE setMinValues NOTIFY meanValuesChanged);
    Q_PROPERTY(QList<int> meanValues READ meanValues WRITE setMeanValues NOTIFY meanValuesChanged);
    Q_PROPERTY(QList<int> maxValues READ maxValues WRITE setMaxValues NOTIFY maxValuesChanged);

public:
    //TODO: Export the model to be used via QML
    // Currently it's hardcoded.
    ColumnStatisticsWrapper(QObject *parent = 0);

    /* for a Column based statistics, QtCharts needs vectors,
    * where each value represents a column. */
    void setColumnNames(const QList<QString>& newColumnNames);
    void setMinValues(const QList<int>& values);
    void setMeanValues(const QList<int>& values);
    void setMaxValues(const QList<int>& values);

    QList<QString> columnNames() const;
    QList<int> maxValues() const;
    QList<int> minValues() const;
    QList<int> meanValues() const;

    virtual void repopulateData() = 0;
private:
    QList<QString> m_columnNames;
    QList<int> m_meanValues;
    QList<int> m_minValues;
    QList<int> m_maxValues;

signals:
    void columnNamesChanged(const QList<QString>& names);
    void minValuesChanged(const QList<int>& minValues);
    void meanValuesChanged(const QList<int>& meanValues);
    void maxValuesChanged(const QList<int>& maxValues);
};

class TripDepthStatistics : public ColumnStatisticsWrapper {
    Q_OBJECT
public:
    TripDepthStatistics(QObject *parent = 0);
    void repopulateData() override;
};

class TabDiveStatistics : public TabBase {
	Q_OBJECT
public:
	TabDiveStatistics(QWidget *parent = 0);
	~TabDiveStatistics();
	void updateData() override;
	void clear() override;
private:
    TripDepthStatistics *m_columnsDepth; // if one works, all will.
};

#endif
