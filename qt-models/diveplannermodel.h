#ifndef DIVEPLANNERMODEL_H
#define DIVEPLANNERMODEL_H

#include <QAbstractTableModel>
#include <QDateTime>

#include "core/dive.h"

class DivePlannerPointsModel : public QAbstractTableModel {
	Q_OBJECT
public:
	static DivePlannerPointsModel *instance();
	enum Sections {
		REMOVE,
		DEPTH,
		DURATION,
		RUNTIME,
		GAS,
		CCSETPOINT,
		COLUMNS
	};
	enum Mode {
		NOTHING,
		PLAN,
		ADD
	};
	virtual int columnCount(const QModelIndex &parent = QModelIndex()) const;
	virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
	virtual int rowCount(const QModelIndex &parent = QModelIndex()) const;
	virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
	virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
	virtual Qt::ItemFlags flags(const QModelIndex &index) const;
	void gaschange(const QModelIndex &index, int newcylinderid);
	void removeSelectedPoints(const QVector<int> &rows);
	void setPlanMode(Mode mode);
	bool isPlanner();
	void createSimpleDive();
	void setupStartTime();
	void clear();
	Mode currentMode() const;
	bool setRecalc(bool recalc);
	bool recalcQ();
	bool tankInUse(int cylinderid);
	void setupCylinders();
	bool updateMaxDepth();
	/**
	 * @return the row number.
	 */
	void editStop(int row, divedatapoint newData);
	divedatapoint at(int row);
	int size();
	struct diveplan &getDiveplan();
	QStringList &getGasList();
	int lastEnteredPoint();
	void removeDeco();
	static bool addingDeco;

public
slots:
	int addStop(int millimeters = 0, int seconds = 0, int cylinderid_in = -1, int ccpoint = 0, bool entered = true);
	void addCylinder_clicked();
	void setGFHigh(const int gfhigh);
	void triggerGFHigh();
	void setGFLow(const int ghflow);
	void triggerGFLow();
	void setVpmbConservatism(int level);
	void setSurfacePressure(int pressure);
	void setSalinity(int salinity);
	int getSurfacePressure();
	void setBottomSac(double sac);
	void setDecoSac(double sac);
	void setStartTime(const QTime &t);
	void setStartDate(const QDate &date);
	void setLastStop6m(bool value);
	void setDropStoneMode(bool value);
	void setVerbatim(bool value);
	void setDisplayRuntime(bool value);
	void setDisplayDuration(bool value);
	void setDisplayTransitions(bool value);
	void setDecoMode(int mode);
	void setSafetyStop(bool value);
	void savePlan();
	void saveDuplicatePlan();
	void remove(const QModelIndex &index);
	void cancelPlan();
	void createTemporaryPlan();
	void deleteTemporaryPlan();
	void loadFromDive(dive *d);
	void emitDataChanged();
	void setRebreatherMode(int mode);
	void setReserveGas(int reserve);
	void setSwitchAtReqStop(bool value);
	void setMinSwitchDuration(int duration);

signals:
	void planCreated();
	void planCanceled();
	void cylinderModelEdited();
	void startTimeChanged(QDateTime);
	void recreationChanged(bool);
	void calculatedPlanNotes();

private:
	explicit DivePlannerPointsModel(QObject *parent = 0);
	void createPlan(bool replanCopy);
	struct diveplan diveplan;
	Mode mode;
	bool recalc;
	QVector<divedatapoint> divepoints;
	QDateTime startTime;
	int tempGFHigh;
	int tempGFLow;
};

#endif
