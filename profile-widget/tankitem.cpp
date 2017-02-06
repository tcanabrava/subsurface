#include "profile-widget/tankitem.h"
#include "qt-models/diveplotdatamodel.h"
#include "profile-widget/divetextitem.h"
#include "core/profile.h"
#include <QPen>

TankItem::TankItem(QObject *parent) :
	QObject(parent),
	QGraphicsRectItem(),
	dataModel(0),
	pInfoEntry(0),
	pInfoNr(0)
{
	height = 3;
	QColor red(PERSIANRED1);
	QColor blue(AIR_BLUE);
	QColor yellow(NITROX_YELLOW);
	QColor green(NITROX_GREEN);
	QLinearGradient nitroxGradient(QPointF(0, 0), QPointF(0, height));
	nitroxGradient.setColorAt(0.0, green);
	nitroxGradient.setColorAt(0.49, green);
	nitroxGradient.setColorAt(0.5, yellow);
	nitroxGradient.setColorAt(1.0, yellow);
	nitrox = nitroxGradient;
	oxygen = green;
	QLinearGradient trimixGradient(QPointF(0, 0), QPointF(0, height));
	trimixGradient.setColorAt(0.0, green);
	trimixGradient.setColorAt(0.49, green);
	trimixGradient.setColorAt(0.5, red);
	trimixGradient.setColorAt(1.0, red);
	trimix = trimixGradient;
	air = blue;
	memset(&diveCylinderStore, 0, sizeof(diveCylinderStore));
}

TankItem::~TankItem()
{
	// Should this be clear_dive(diveCylinderStore)?
	for (int i = 0; i < MAX_CYLINDERS; i++)
		free((void *)diveCylinderStore.cylinder[i].type.description);
}

void TankItem::setData(DivePlotDataModel *model, struct plot_info *plotInfo, struct dive *d)
{
	free(pInfoEntry);
	// the plotInfo and dive structures passed in could become invalid before we stop using them,
	// so copy the data that we need
	int size = plotInfo->nr * sizeof(plotInfo->entry[0]);
	pInfoEntry = (struct plot_data *)malloc(size);
	pInfoNr = plotInfo->nr;
	memcpy(pInfoEntry, plotInfo->entry, size);
	copy_cylinders(d, &diveCylinderStore, false);
	dataModel = model;
	connect(dataModel, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(modelDataChanged(QModelIndex, QModelIndex)), Qt::UniqueConnection);
	modelDataChanged();
}

void TankItem::createBar(qreal x, qreal w, struct gasmix *gas)
{
	// pick the right gradient, size, position and text
	QGraphicsRectItem *rect = new QGraphicsRectItem(x, 0, w, height, this);
	if (gasmix_is_air(gas))
		rect->setBrush(air);
	else if (gas->he.permille)
		rect->setBrush(trimix);
	else if (gas->o2.permille == 1000)
		rect->setBrush(oxygen);
	else
		rect->setBrush(nitrox);
	rect->setPen(QPen(QBrush(), 0.0)); // get rid of the thick line around the rectangle
	rects.push_back(rect);
	DiveTextItem *label = new DiveTextItem(rect);
	label->setText(gasname(gas));
	label->setBrush(Qt::black);
	label->setPos(x + 1, 0);
	label->setAlignment(Qt::AlignBottom | Qt::AlignRight);
#ifdef SUBSURFACE_MOBILE
	label->setPos(x + 1, -2.5);
#endif
	label->setZValue(101);
}

void TankItem::modelDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
	Q_UNUSED(topLeft);
	Q_UNUSED(bottomRight);
	// We don't have enougth data to calculate things, quit.
	if (!dataModel || !pInfoEntry || !pInfoNr)
		return;

	// remove the old rectangles
	foreach (QGraphicsRectItem *r, rects) {
		delete(r);
	}
	rects.clear();

	// walk the list and figure out which tanks go where
	struct plot_data *entry = pInfoEntry;
	struct plot_data *lastentry = pInfoEntry;
	int cylIdx = entry->cylinderindex;
	int i = -1;
	int startTime = 0;
	struct gasmix *gas = &diveCylinderStore.cylinder[cylIdx].gasmix;
	qreal width, left;
	while (++i < pInfoNr) {
		entry = &pInfoEntry[i];
		lastentry = &pInfoEntry[i-1];
		if (entry->cylinderindex == cylIdx)
			continue;
		width = hAxis->posAtValue(lastentry->sec) - hAxis->posAtValue(startTime);
		left = hAxis->posAtValue(startTime);
		createBar(left, width, gas);
		cylIdx = entry->cylinderindex;
		gas = &diveCylinderStore.cylinder[cylIdx].gasmix;
		startTime = lastentry->sec;
	}
	width = hAxis->posAtValue(entry->sec) - hAxis->posAtValue(startTime);
	left = hAxis->posAtValue(startTime);
	createBar(left, width, gas);
}

void TankItem::setHorizontalAxis(DiveCartesianAxis *horizontal)
{
	hAxis = horizontal;
	connect(hAxis, SIGNAL(sizeChanged()), this, SLOT(modelDataChanged()));
	modelDataChanged();
}
