#include "profile-widget/divetooltipitem.h"
#include "profile-widget/divecartesianaxis.h"
#include "core/dive.h"
#include "core/profile.h"
#include "core/membuffer.h"
#include "core/metrics.h"
#include <QPropertyAnimation>
#include <QSettings>
#include <QGraphicsView>
#include <QStyleOptionGraphicsItem>
#include "core/qthelper.h"

void ToolTipItem::addToolTip(const QString &toolTip, const QIcon &icon, const QPixmap& pixmap)
{
	const IconMetrics& iconMetrics = defaultIconMetrics();

	QGraphicsPixmapItem *iconItem = 0;
	double yValue = title->boundingRect().height() + iconMetrics.spacing;
	Q_FOREACH (ToolTip t, toolTips) {
		yValue += t.second->boundingRect().height();
	}
	if (entryToolTip.second) {
		yValue += entryToolTip.second->boundingRect().height();
	}
	iconItem = new QGraphicsPixmapItem(this);
	if (!icon.isNull()) {
		iconItem->setPixmap(icon.pixmap(iconMetrics.sz_small, iconMetrics.sz_small));
	} else if (!pixmap.isNull()) {
		iconItem->setPixmap(pixmap);
	}
	const int sp2 = iconMetrics.spacing * 2;
	iconItem->setPos(sp2, yValue);

	QGraphicsSimpleTextItem *textItem = new QGraphicsSimpleTextItem(toolTip, this);
	textItem->setPos(sp2 + iconMetrics.sz_small + sp2, yValue);
	textItem->setBrush(QBrush(Qt::white));
	textItem->setFlag(ItemIgnoresTransformations);
	toolTips.push_back(qMakePair(iconItem, textItem));
}

void ToolTipItem::clear()
{
	Q_FOREACH (ToolTip t, toolTips) {
		delete t.first;
		delete t.second;
	}
	toolTips.clear();
}

void ToolTipItem::setRect(const QRectF &r)
{
	if( r == rect() ) {
		return;
	}

	QGraphicsRectItem::setRect(r);
	updateTitlePosition();
}

void ToolTipItem::collapse()
{
	int dim = defaultIconMetrics().sz_small;

	if (prefs.animation_speed) {
		QPropertyAnimation *animation = new QPropertyAnimation(this, "rect");
		animation->setDuration(100);
		animation->setStartValue(nextRectangle);
		animation->setEndValue(QRect(0, 0, dim, dim));
		animation->start(QAbstractAnimation::DeleteWhenStopped);
	} else {
		setRect(nextRectangle);
	}
	clear();

	status = COLLAPSED;
}

void ToolTipItem::expand()
{
	if (!title)
		return;

	const IconMetrics& iconMetrics = defaultIconMetrics();

	double width = 0, height = title->boundingRect().height() + iconMetrics.spacing;
	Q_FOREACH (const ToolTip& t, toolTips) {
		QRectF sRect = t.second->boundingRect();
		if (sRect.width() > width)
			width = sRect.width();
		height += sRect.height();
	}

	if (entryToolTip.first) {
		QRectF sRect = entryToolTip.second->boundingRect();
		if (sRect.width() > width)
			width = sRect.width();
		height += sRect.height();
	}

	const int sp2 = iconMetrics.spacing * 2;
	// pixmap left padding, icon, pixmap right padding, right padding */
	width += sp2 + iconMetrics.sz_small + sp2 + sp2 * 2;
	// bottom padding
	height += sp2;

	// clip the tooltip width
	if (width < title->boundingRect().width() + sp2)
		width = title->boundingRect().width() + sp2;
	// clip the height
	if (entryToolTip.first) {
		const int minH = entryToolTip.first->y() + entryToolTip.first->pixmap().height() + sp2;
		if (height < minH)
			height = minH;
	} else if (height < iconMetrics.sz_small) {
		height = iconMetrics.sz_small;
	}

	nextRectangle.setWidth(width);
	nextRectangle.setHeight(height);

	if (nextRectangle != rect()) {
		if (prefs.animation_speed) {
			QPropertyAnimation *animation = new QPropertyAnimation(this, "rect", this);
			animation->setDuration(prefs.animation_speed);
			animation->setStartValue(rect());
			animation->setEndValue(nextRectangle);
			animation->start(QAbstractAnimation::DeleteWhenStopped);
		} else {
			setRect(nextRectangle);
		}
	}

	status = EXPANDED;
}

ToolTipItem::ToolTipItem(QGraphicsItem *parent) : QGraphicsRectItem(parent),
	title(new QGraphicsSimpleTextItem(tr("Information"), this)),
	status(COLLAPSED),
	timeAxis(0),
	lastTime(-1)
{
	memset(&pInfo, 0, sizeof(pInfo));
	entryToolTip.first = NULL;
	entryToolTip.second = NULL;
	setFlags(ItemIgnoresTransformations | ItemIsMovable | ItemClipsChildrenToShape);

	QColor c = QColor(Qt::black);
	c.setAlpha(155);
	setBrush(c);

	setZValue(99);

	addToolTip(QString(), QIcon(), QPixmap(16,60));
	entryToolTip = toolTips.first();
	toolTips.clear();

	title->setFlag(ItemIgnoresTransformations);
	title->setPen(QPen(Qt::white, 1));
	title->setBrush(Qt::white);

	setPen(QPen(Qt::white, 2));
	refreshTime.start();
}

ToolTipItem::~ToolTipItem()
{
	clear();
}

void ToolTipItem::updateTitlePosition()
{
	const IconMetrics& iconMetrics = defaultIconMetrics();
	if (rect().width() < title->boundingRect().width() + iconMetrics.spacing * 4) {
		QRectF newRect = rect();
		newRect.setWidth(title->boundingRect().width() + iconMetrics.spacing * 4);
		newRect.setHeight((newRect.height() && isExpanded()) ? newRect.height() : iconMetrics.sz_small);
		setRect(newRect);
	}

	title->setPos(rect().width() / 2 - title->boundingRect().width() / 2 - 1, 0);
}

bool ToolTipItem::isExpanded() const
{
	return status == EXPANDED;
}

void ToolTipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	persistPos();
	QGraphicsRectItem::mouseReleaseEvent(event);
	Q_FOREACH (QGraphicsItem *item, oldSelection) {
		item->setSelected(true);
	}
}

void ToolTipItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	Q_UNUSED(widget);
	painter->save();
	painter->setClipRect(option->rect);
	painter->setPen(pen());
	painter->setBrush(brush());
	painter->drawRoundedRect(rect(), 8, 8, Qt::AbsoluteSize);
	painter->restore();
}

void ToolTipItem::persistPos()
{
	QSettings s;
	s.beginGroup("ProfileMap");
	s.setValue("tooltip_position", pos());
	s.endGroup();
}

void ToolTipItem::readPos()
{
	QSettings s;
	s.beginGroup("ProfileMap");
	QPointF value = s.value("tooltip_position").toPoint();
	if (!scene()->sceneRect().contains(value)) {
		value = QPointF(0, 0);
	}
	setPos(value);
}

void ToolTipItem::setPlotInfo(const plot_info &plot)
{
	pInfo = plot;
}

void ToolTipItem::setTimeAxis(DiveCartesianAxis *axis)
{
	timeAxis = axis;
}

void ToolTipItem::refresh(const QPointF &pos)
{
	struct plot_data *entry;
	static QPixmap tissues(16,60);
	static QPainter painter(&tissues);
	static struct membuffer mb = {};

	if(refreshTime.elapsed() < 40)
		return;
	refreshTime.start();

	int time = timeAxis->valueAt(pos);
	if (time == lastTime)
		return;

	lastTime = time;
	clear();

	mb.len = 0;
	entry = get_plot_details_new(&pInfo, time, &mb);

	tissues.fill();
	painter.setPen(QColor(0, 0, 0, 0));
	painter.setBrush(QColor(LIMENADE1));
	painter.drawRect(0, 10 + (100 - AMB_PERCENTAGE) / 2, 16, AMB_PERCENTAGE / 2);
	painter.setBrush(QColor(SPRINGWOOD1));
	painter.drawRect(0, 10, 16, (100 - AMB_PERCENTAGE) / 2);
	painter.setBrush(QColor(Qt::red));
	painter.drawRect(0,0,16,10);
	if (entry) {
		ProfileWidget2 *view = qobject_cast<ProfileWidget2*>(scene()->views().first());
		Q_ASSERT(view);

		painter.setPen(QColor(0, 0, 0, 255));
		if (decoMode() == BUEHLMANN)
			painter.drawLine(0, 60 - entry->gfline / 2, 16, 60 - entry->gfline / 2);
		painter.drawLine(0, 60 - AMB_PERCENTAGE * (entry->pressures.n2 + entry->pressures.he) / entry->ambpressure / 2,
				16, 60 - AMB_PERCENTAGE * (entry->pressures.n2 + entry->pressures.he) / entry->ambpressure /2);
		painter.setPen(QColor(0, 0, 0, 127));
		for (int i=0; i<16; i++) {
			painter.drawLine(i, 60, i, 60 - entry->percentages[i] / 2);
		}
		entryToolTip.second->setText(QString::fromUtf8(mb.buffer, mb.len));
	}
	entryToolTip.first->setPixmap(tissues);

	Q_FOREACH (QGraphicsItem *item, scene()->items(pos, Qt::IntersectsItemBoundingRect
		,Qt::DescendingOrder, scene()->views().first()->transform())) {
		if (!item->toolTip().isEmpty())
			addToolTip(item->toolTip());
	}
	expand();
}

void ToolTipItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	oldSelection = scene()->selectedItems();
	scene()->clearSelection();
	QGraphicsItem::mousePressEvent(event);
}
