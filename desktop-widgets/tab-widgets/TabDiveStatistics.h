// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_STATISTICS_H
#define TAB_DIVE_STATISTICS_H

#include "TabBase.h"
#include <QString>
#include <QList>
#include <QVariant>
#include <QAbstractTableModel>
#include <QVector>

/* I currently have no idea on how to deal with this.
 */

struct MinAvgMax {
    qreal min;
    qreal avg;
    qreal max;
};

class MinAvgMaxModel : public QAbstractTableModel {
    Q_OBJECT
public:
    MinAvgMaxModel(QObject *parent = 0);
    ~MinAvgMaxModel();
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual void repopulateData() = 0;

protected:
    QVector<MinAvgMax> internalData;
    QList<QString> rowHeaders;
};

class TripDepthModel : public MinAvgMaxModel {
    Q_OBJECT
public:
    TripDepthModel(QObject *parent);
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
};

#endif
