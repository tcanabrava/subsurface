// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"

/* I currently have no idea on how to deal with this.
 */
class ColumnStatisticsWrapper : public QObject {
    Q_OBJECT
public:
    /* for a Column based statistics, QtCharts needs vectors,
    * where each value represents a column. */
    void setColumnNames(const QList<QString>& newColumnNames);
    void setMinValues(const QList<int>& minValues);
    void setMeanValues(const QList<int>& meanValues);
    void setMaxValues(const QList<int>& maxValues);

    Q_INVOKABLE QList<QString> columnNames();
    Q_INVOKABLE QList<int> maxValues();
    Q_INVOKABLE QList<int> meanValues();
    Q_INVOKABLE QList<int> minValues();

private:
    QList<QString> m_columnNames;
    QList<int> m_meanValues;
    QList<int> m_minValues;
    QList<int> m_maxValues;

signals:
    void changed(); // triggered when min/max/mean value changes.
};

class TabDiveStatistics : public TabBase {
	Q_OBJECT
public:
	TabDiveStatistics(QWidget *parent = 0);
	~TabDiveStatistics();
	void updateData() override;
	void clear() override;
private:
    ColumnStatisticsWrapper *m_columnsDepth; // if one works, all will.
};

#endif
