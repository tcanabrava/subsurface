// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>
#include <QVariant>

/* I currently have no idea on how to deal with this.
 */
class ColumnStatisticsWrapper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList names READ columnNames WRITE setColumnNames NOTIFY columnNamesChanged);
    Q_PROPERTY(QVariantList minValues READ minValues WRITE setMinValues NOTIFY minValuesChanged);
    Q_PROPERTY(QVariantList meanValues READ meanValues WRITE setMeanValues NOTIFY meanValuesChanged);
    Q_PROPERTY(QVariantList maxValues READ maxValues WRITE setMaxValues NOTIFY maxValuesChanged);

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

    QStringList columnNames() const;
    QVariantList maxValues() const;
    QVariantList minValues() const;
    QVariantList meanValues() const;

    virtual void repopulateData() = 0;
private:
    QStringList m_columnNames;
    QVariantList m_meanValues;
    QVariantList m_minValues;
    QVariantList m_maxValues;

signals:
    void columnNamesChanged(const QStringList& names);
    void minValuesChanged(const  QVariantList& minValues);
    void meanValuesChanged(const QVariantList& meanValues);
    void maxValuesChanged(const  QVariantList& maxValues);
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
