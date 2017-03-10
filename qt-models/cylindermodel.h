#ifndef CYLINDERMODEL_H
#define CYLINDERMODEL_H

#include "cleanertablemodel.h"
#include "core/dive.h"

/* Encapsulation of the Cylinder Model, that presents the
 * Current cylinders that are used on a dive. */
class CylindersModel : public CleanerTableModel {
	Q_OBJECT
public:
	enum Column {
		REMOVE,
		TYPE,
		SIZE,
		WORKINGPRESS,
		START,
		END,
		O2,
		HE,
		DEPTH,
		MOD,
		MND,
		USE,
		COLUMNS
	};

	explicit CylindersModel(QObject *parent = 0);
	static CylindersModel *instance();
	/*reimp*/ QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
	/*reimp*/ int rowCount(const QModelIndex &parent = QModelIndex()) const;
	/*reimp*/ Qt::ItemFlags flags(const QModelIndex &index) const;
	/*reimp*/ bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);

	void passInData(const QModelIndex &index, const QVariant &value);
	void add();
	void clear();
	void updateDive();
	void copyFromDive(struct dive *d);
	void updateDecoDepths(pressure_t olddecopo2);
	cylinder_t *cylinderAt(const QModelIndex &index);
	bool changed;
	virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;

public
slots:
	void remove(const QModelIndex &index);
	bool updateBestMixes();

private:
	int rows;
};

#endif
