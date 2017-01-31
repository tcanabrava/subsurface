#include "desktop-widgets/configuredivecomputerdialog.h"

#include "core/helpers.h"
#include "desktop-widgets/mainwindow.h"
#include "core/display.h"
#include "core/subsurface-qt/SettingsObjectWrapper.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QNetworkReply>
#include <QProgressDialog>

struct product {
	const char *product;
	dc_descriptor_t *descriptor;
	struct product *next;
};

struct vendor {
	const char *vendor;
	struct product *productlist;
	struct vendor *next;
};

struct mydescriptor {
	const char *vendor;
	const char *product;
	dc_family_t type;
	unsigned int model;
};

GasSpinBoxItemDelegate::GasSpinBoxItemDelegate(QObject *parent, column_type type) : QStyledItemDelegate(parent), type(type)
{
}
GasSpinBoxItemDelegate::~GasSpinBoxItemDelegate()
{
}

QWidget *GasSpinBoxItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)
	Q_UNUSED(index)
	// Create the spinbox and give it it's settings
	QSpinBox *sb = new QSpinBox(parent);
	if (type == PERCENT) {
		sb->setMinimum(0);
		sb->setMaximum(100);
		sb->setSuffix("%");
	} else if (type == DEPTH) {
		sb->setMinimum(0);
		sb->setMaximum(255);
		sb->setSuffix(" m");
	} else if (type == SETPOINT) {
		sb->setMinimum(0);
		sb->setMaximum(255);
		sb->setSuffix(" cbar");
	}
	return sb;
}

void GasSpinBoxItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
	if (QSpinBox *sb = qobject_cast<QSpinBox *>(editor))
		sb->setValue(index.data(Qt::EditRole).toInt());
	else
		QStyledItemDelegate::setEditorData(editor, index);
}


void GasSpinBoxItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	if (QSpinBox *sb = qobject_cast<QSpinBox *>(editor))
		model->setData(index, sb->value(), Qt::EditRole);
	else
		QStyledItemDelegate::setModelData(editor, model, index);
}

GasTypeComboBoxItemDelegate::GasTypeComboBoxItemDelegate(QObject *parent, computer_type type) : QStyledItemDelegate(parent), type(type)
{
}
GasTypeComboBoxItemDelegate::~GasTypeComboBoxItemDelegate()
{
}

QWidget *GasTypeComboBoxItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)
	Q_UNUSED(index)
	// Create the combobox and populate it
	QComboBox *cb = new QComboBox(parent);
	cb->addItem(QString("Disabled"));
	if (type == OSTC3) {
		cb->addItem(QString("First"));
		cb->addItem(QString("Travel"));
		cb->addItem(QString("Deco"));
	} else if (type == OSTC) {
		cb->addItem(QString("Active"));
		cb->addItem(QString("First"));
	}
	return cb;
}

void GasTypeComboBoxItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
	if (QComboBox *cb = qobject_cast<QComboBox *>(editor))
		cb->setCurrentIndex(index.data(Qt::EditRole).toInt());
	else
		QStyledItemDelegate::setEditorData(editor, index);
}


void GasTypeComboBoxItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	if (QComboBox *cb = qobject_cast<QComboBox *>(editor))
		model->setData(index, cb->currentIndex(), Qt::EditRole);
	else
		QStyledItemDelegate::setModelData(editor, model, index);
}

ConfigureDiveComputerDialog::ConfigureDiveComputerDialog(QWidget *parent) : QDialog(parent),
	config(0),
#ifdef BT_SUPPORT
	deviceDetails(0),
	btDeviceSelectionDialog(0)
#else
	deviceDetails(0)
#endif
{
	ui.setupUi(this);

	deviceDetails = new DeviceDetails(this);
	config = new ConfigureDiveComputer();
	connect(config, SIGNAL(progress(int)), ui.progressBar, SLOT(setValue(int)));
	connect(config, SIGNAL(error(QString)), this, SLOT(configError(QString)));
	connect(config, SIGNAL(message(QString)), this, SLOT(configMessage(QString)));
	connect(config, SIGNAL(deviceDetailsChanged(DeviceDetails *)),
		this, SLOT(deviceDetailsReceived(DeviceDetails *)));
	connect(ui.retrieveDetails, SIGNAL(clicked()), this, SLOT(readSettings()));
	connect(ui.resetButton, SIGNAL(clicked()), this, SLOT(resetSettings()));
	ui.chooseLogFile->setEnabled(ui.logToFile->isChecked());
	connect(ui.chooseLogFile, SIGNAL(clicked()), this, SLOT(pickLogFile()));
	connect(ui.logToFile, SIGNAL(stateChanged(int)), this, SLOT(checkLogFile(int)));
	connect(ui.connectButton, SIGNAL(clicked()), this, SLOT(dc_open()));
	connect(ui.disconnectButton, SIGNAL(clicked()), this, SLOT(dc_close()));
#ifdef BT_SUPPORT
	connect(ui.bluetoothMode, SIGNAL(clicked(bool)), this, SLOT(selectRemoteBluetoothDevice()));
#else
	ui.bluetoothMode->setVisible(false);
#endif

	memset(&device_data, 0, sizeof(device_data));
	fill_computer_list();
	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	if (!dc->dc_device().isEmpty())
		ui.device->setEditText(dc->dc_device());

	ui.DiveComputerList->setCurrentRow(0);
	on_DiveComputerList_currentRowChanged(0);

	ui.ostc3GasTable->setItemDelegateForColumn(1, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::PERCENT));
	ui.ostc3GasTable->setItemDelegateForColumn(2, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::PERCENT));
	ui.ostc3GasTable->setItemDelegateForColumn(3, new GasTypeComboBoxItemDelegate(this, GasTypeComboBoxItemDelegate::OSTC3));
	ui.ostc3GasTable->setItemDelegateForColumn(4, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));
	ui.ostc3DilTable->setItemDelegateForColumn(3, new GasTypeComboBoxItemDelegate(this, GasTypeComboBoxItemDelegate::OSTC3));
	ui.ostc3DilTable->setItemDelegateForColumn(4, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));
	ui.ostc3SetPointTable->setItemDelegateForColumn(1, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::SETPOINT));
	ui.ostc3SetPointTable->setItemDelegateForColumn(2, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));
	ui.ostcGasTable->setItemDelegateForColumn(1, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::PERCENT));
	ui.ostcGasTable->setItemDelegateForColumn(2, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::PERCENT));
	ui.ostcGasTable->setItemDelegateForColumn(3, new GasTypeComboBoxItemDelegate(this, GasTypeComboBoxItemDelegate::OSTC));
	ui.ostcGasTable->setItemDelegateForColumn(4, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));
	ui.ostcDilTable->setItemDelegateForColumn(3, new GasTypeComboBoxItemDelegate(this, GasTypeComboBoxItemDelegate::OSTC));
	ui.ostcDilTable->setItemDelegateForColumn(4, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));
	ui.ostcSetPointTable->setItemDelegateForColumn(1, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::SETPOINT));
	ui.ostcSetPointTable->setItemDelegateForColumn(2, new GasSpinBoxItemDelegate(this, GasSpinBoxItemDelegate::DEPTH));

	QSettings settings;
	settings.beginGroup("ConfigureDiveComputerDialog");
	settings.beginGroup("ostc3GasTable");
	for (int i = 0; i < ui.ostc3GasTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostc3GasTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();
	settings.beginGroup("ostc3DilTable");
	for (int i = 0; i < ui.ostc3DilTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostc3DilTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();
	settings.beginGroup("ostc3SetPointTable");
	for (int i = 0; i < ui.ostc3SetPointTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostc3SetPointTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();

	settings.beginGroup("ostcGasTable");
	for (int i = 0; i < ui.ostcGasTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostcGasTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();
	settings.beginGroup("ostcDilTable");
	for (int i = 0; i < ui.ostcDilTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostcDilTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();
	settings.beginGroup("ostcSetPointTable");
	for (int i = 0; i < ui.ostcSetPointTable->columnCount(); i++) {
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			ui.ostcSetPointTable->setColumnWidth(i, width.toInt());
	}
	settings.endGroup();
	settings.endGroup();
}

OstcFirmwareCheck::OstcFirmwareCheck(QString product) : parent(0)
{
	QUrl url;
	memset(&devData, 1, sizeof(devData));
	if (product == "OSTC 3" || product == "OSTC 3+" || product == "OSTC Cr") {
		url = QUrl("http://www.heinrichsweikamp.net/autofirmware/ostc3_changelog.txt");
		latestFirmwareHexFile = QString("http://www.heinrichsweikamp.net/autofirmware/ostc3_firmware.hex");
	} else if (product == "OSTC Sport") {
		url = QUrl("http://www.heinrichsweikamp.net/autofirmware/ostc_sport_changelog.txt");
		latestFirmwareHexFile = QString("http://www.heinrichsweikamp.net/autofirmware/ostc_sport_firmware.hex");
	} else if (product == "OSTC 4") {
		url = QUrl("http://www.heinrichsweikamp.net/autofirmware/ostc4_changelog.txt");
		latestFirmwareHexFile = QString("http://www.heinrichsweikamp.net/autofirmware/ostc4_firmware.bin");
	} else { // not one of the known dive computers
		return;
	}
	connect(&manager, SIGNAL(finished(QNetworkReply *)), this, SLOT(parseOstcFwVersion(QNetworkReply *)));
	QNetworkRequest download(url);
	manager.get(download);
}

void OstcFirmwareCheck::parseOstcFwVersion(QNetworkReply *reply)
{
	QString parse = reply->readAll();
	int firstOpenBracket = parse.indexOf('[');
	int firstCloseBracket = parse.indexOf(']');
	latestFirmwareAvailable = parse.mid(firstOpenBracket + 1, firstCloseBracket - firstOpenBracket - 1);
	disconnect(&manager, SIGNAL(finished(QNetworkReply *)), this, SLOT(parseOstcFwVersion(QNetworkReply *)));
}

void OstcFirmwareCheck::checkLatest(QWidget *_parent, device_data_t *data)
{
	devData = *data;
	parent = _parent;
	// If we didn't find a current firmware version stop this hole thing here.
	if (latestFirmwareAvailable.isEmpty())
		return;

	// libdivecomputer gives us the firmware on device as an integer
	// for the OSTC that means highbyte.lowbyte is the version number
	// For OSTC 4's its stored as XXXX XYYY YYZZ ZZZB, -> X.Y.Z beta?

	int firmwareOnDevice = devData.libdc_firmware;
	QString firmwareOnDeviceString;
	// Convert the latestFirmwareAvailable to a integear we can compare with
	QStringList fwParts = latestFirmwareAvailable.split(".");
	int latestFirmwareAvailableNumber;

	if (strcmp(data->product, "OSTC 4") == 0) {
		unsigned char X, Y, Z, beta;
		X = (firmwareOnDevice & 0xF800) >> 11;
		Y = (firmwareOnDevice & 0x07C0) >> 6;
		Z = (firmwareOnDevice & 0x003E) >> 1;
		beta = firmwareOnDevice & 0x0001;
		firmwareOnDeviceString = QString("%1.%2.%3%4").arg(X).arg(Y).arg(Z).arg(beta?" beta":"");
		latestFirmwareAvailableNumber = (fwParts[0].toInt() << 11) + (fwParts[1].toInt() << 6) + (fwParts[2].toInt() << 1);
	} else { // OSTC 3, Sport, Cr
		firmwareOnDeviceString = QString("%1.%2").arg(firmwareOnDevice / 256).arg(firmwareOnDevice % 256);
		latestFirmwareAvailableNumber = fwParts[0].toInt() * 256 + fwParts[1].toInt();
	}

	if (latestFirmwareAvailableNumber > firmwareOnDevice) {
		QMessageBox response(parent);
		QString message = tr("You should update the firmware on your dive computer: you have version %1 but the latest stable version is %2")
					  .arg(firmwareOnDeviceString)
					  .arg(latestFirmwareAvailable);
		message += tr("\n\nIf your device uses Bluetooth, do the same preparations as for a logbook download before continuing with the update");
		response.addButton(tr("Not now"), QMessageBox::RejectRole);
		response.addButton(tr("Update firmware"), QMessageBox::AcceptRole);
		response.setText(message);
		response.setWindowTitle(tr("Firmware upgrade notice"));
		response.setIcon(QMessageBox::Question);
		response.setWindowModality(Qt::WindowModal);
		int ret = response.exec();
		if (ret == QMessageBox::Accepted)
			upgradeFirmware();
	}
}

void OstcFirmwareCheck::upgradeFirmware()
{
	// start download of latestFirmwareHexFile
	QString saveFileName = latestFirmwareHexFile;
	saveFileName.replace("http://www.heinrichsweikamp.net/autofirmware/", "");
	saveFileName.replace("firmware", latestFirmwareAvailable);
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append(saveFileName);
	storeFirmware = QFileDialog::getSaveFileName(parent, tr("Save the downloaded firmware as"),
						     filename, tr("Firmware files (*.hex *.bin)"));
	if (storeFirmware.isEmpty())
		return;

	connect(&manager, SIGNAL(finished(QNetworkReply *)), this, SLOT(saveOstcFirmware(QNetworkReply *)));
	QNetworkRequest download(latestFirmwareHexFile);
	manager.get(download);
}

void OstcFirmwareCheck::saveOstcFirmware(QNetworkReply *reply)
{
	// firmware is downloaded
	// call config->startFirmwareUpdate() with that file and the device data

	QByteArray firmwareData = reply->readAll();
	QFile file(storeFirmware);
	file.open(QIODevice::WriteOnly);
	file.write(firmwareData);
	file.close();
	QProgressDialog *dialog = new QProgressDialog("Updating firmware", "", 0, 100);
	dialog->setCancelButton(0);
	dialog->setAutoClose(true);
	ConfigureDiveComputer *config = new ConfigureDiveComputer();
	connect(config, SIGNAL(message(QString)), dialog, SLOT(setLabelText(QString)));
	connect(config, SIGNAL(error(QString)), dialog, SLOT(setLabelText(QString)));
	connect(config, SIGNAL(progress(int)), dialog, SLOT(setValue(int)));
	connect(dialog, SIGNAL(finished(int)), config, SLOT(dc_close()));
	config->dc_open(&devData);
	config->startFirmwareUpdate(storeFirmware, &devData);
}

ConfigureDiveComputerDialog::~ConfigureDiveComputerDialog()
{
	delete config;
}

void ConfigureDiveComputerDialog::closeEvent(QCloseEvent *event)
{
	Q_UNUSED(event)
	dc_close();

	QSettings settings;
	settings.beginGroup("ConfigureDiveComputerDialog");
	settings.beginGroup("ostc3GasTable");
	for (int i = 0; i < ui.ostc3GasTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostc3GasTable->columnWidth(i));
	settings.endGroup();
	settings.beginGroup("ostc3DilTable");
	for (int i = 0; i < ui.ostc3DilTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostc3DilTable->columnWidth(i));
	settings.endGroup();
	settings.beginGroup("ostc3SetPointTable");
	for (int i = 0; i < ui.ostc3SetPointTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostc3SetPointTable->columnWidth(i));
	settings.endGroup();

	settings.beginGroup("ostcGasTable");
	for (int i = 0; i < ui.ostcGasTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostcGasTable->columnWidth(i));
	settings.endGroup();
	settings.beginGroup("ostcDilTable");
	for (int i = 0; i < ui.ostcDilTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostcDilTable->columnWidth(i));
	settings.endGroup();
	settings.beginGroup("ostcSetPointTable");
	for (int i = 0; i < ui.ostcSetPointTable->columnCount(); i++)
		settings.setValue(QString("colwidth%1").arg(i), ui.ostcSetPointTable->columnWidth(i));
	settings.endGroup();
	settings.endGroup();
}


static void fillDeviceList(const char *name, void *data)
{
	QComboBox *comboBox = (QComboBox *)data;
	comboBox->addItem(name);
}

void ConfigureDiveComputerDialog::fill_device_list(int dc_type)
{
	int deviceIndex;
	ui.device->clear();
	deviceIndex = enumerate_devices(fillDeviceList, ui.device, dc_type);
	if (deviceIndex >= 0)
		ui.device->setCurrentIndex(deviceIndex);
}

void ConfigureDiveComputerDialog::fill_computer_list()
{
	dc_iterator_t *iterator = NULL;
	dc_descriptor_t *descriptor = NULL;

	struct mydescriptor *mydescriptor;

	dc_descriptor_iterator(&iterator);
	while (dc_iterator_next(iterator, &descriptor) == DC_STATUS_SUCCESS) {
		const char *vendor = dc_descriptor_get_vendor(descriptor);
		const char *product = dc_descriptor_get_product(descriptor);

		if (!vendorList.contains(vendor))
			vendorList.append(vendor);

		if (!productList[vendor].contains(product))
			productList[vendor].push_back(product);

		descriptorLookup[QString(vendor) + QString(product)] = descriptor;
	}
	dc_iterator_free(iterator);

	mydescriptor = (struct mydescriptor *)malloc(sizeof(struct mydescriptor));
	mydescriptor->vendor = "Uemis";
	mydescriptor->product = "Zurich";
	mydescriptor->type = DC_FAMILY_NULL;
	mydescriptor->model = 0;

	if (!vendorList.contains("Uemis"))
		vendorList.append("Uemis");

	if (!productList["Uemis"].contains("Zurich"))
		productList["Uemis"].push_back("Zurich");

	descriptorLookup["UemisZurich"] = (dc_descriptor_t *)mydescriptor;

	qSort(vendorList);
}

void ConfigureDiveComputerDialog::populateDeviceDetails()
{
	switch (ui.dcStackedWidget->currentIndex()) {
	case 0:
		populateDeviceDetailsOSTC3();
		break;
	case 1:
		populateDeviceDetailsSuuntoVyper();
		break;
	case 2:
		populateDeviceDetailsOSTC();
		break;
	}
}

#define GET_INT_FROM(_field, _default) ((_field) != NULL) ? (_field)->data(Qt::EditRole).toInt() : (_default)

void ConfigureDiveComputerDialog::populateDeviceDetailsOSTC3()
{
	deviceDetails->customText = ui.customTextLlineEdit->text();
	deviceDetails->diveMode = ui.diveModeComboBox->currentIndex();
	deviceDetails->saturation = ui.saturationSpinBox->value();
	deviceDetails->desaturation = ui.desaturationSpinBox->value();
	deviceDetails->lastDeco = ui.lastDecoSpinBox->value();
	deviceDetails->brightness = ui.brightnessComboBox->currentIndex();
	deviceDetails->units = ui.unitsComboBox->currentIndex();
	deviceDetails->samplingRate = ui.samplingRateComboBox->currentIndex();
	deviceDetails->salinity = ui.salinitySpinBox->value();
	deviceDetails->diveModeColor = ui.diveModeColour->currentIndex();
	deviceDetails->language = ui.languageComboBox->currentIndex();
	deviceDetails->dateFormat = ui.dateFormatComboBox->currentIndex();
	deviceDetails->compassGain = ui.compassGainComboBox->currentIndex();
	deviceDetails->syncTime = ui.dateTimeSyncCheckBox->isChecked();
	deviceDetails->safetyStop = ui.safetyStopCheckBox->isChecked();
	deviceDetails->gfHigh = ui.gfHighSpinBox->value();
	deviceDetails->gfLow = ui.gfLowSpinBox->value();
	deviceDetails->pressureSensorOffset = ui.pressureSensorOffsetSpinBox->value();
	deviceDetails->ppO2Min = ui.ppO2MinSpinBox->value();
	deviceDetails->ppO2Max = ui.ppO2MaxSpinBox->value();
	deviceDetails->futureTTS = ui.futureTTSSpinBox->value();
	deviceDetails->ccrMode = ui.ccrModeComboBox->currentIndex();
	deviceDetails->decoType = ui.decoTypeComboBox->currentIndex();
	deviceDetails->aGFSelectable = ui.aGFSelectableCheckBox->isChecked();
	deviceDetails->aGFHigh = ui.aGFHighSpinBox->value();
	deviceDetails->aGFLow = ui.aGFLowSpinBox->value();
	deviceDetails->calibrationGas = ui.calibrationGasSpinBox->value();
	deviceDetails->flipScreen = ui.flipScreenCheckBox->isChecked();
	deviceDetails->setPointFallback = ui.setPointFallbackCheckBox->isChecked();
	deviceDetails->leftButtonSensitivity = ui.leftButtonSensitivity->value();
	deviceDetails->rightButtonSensitivity = ui.rightButtonSensitivity->value();
	deviceDetails->bottomGasConsumption = ui.bottomGasConsumption->value();
	deviceDetails->decoGasConsumption = ui.decoGasConsumption->value();
	deviceDetails->modWarning = ui.modWarning->isChecked();
	deviceDetails->dynamicAscendRate = ui.dynamicAscendRate->isChecked();
	deviceDetails->graphicalSpeedIndicator = ui.graphicalSpeedIndicator->isChecked();
	deviceDetails->alwaysShowppO2 = ui.alwaysShowppO2->isChecked();
	deviceDetails->tempSensorOffset = ui.tempSensorOffsetDoubleSpinBox->value() * 10;
	deviceDetails->safetyStopLength = ui.safetyStopLengthSpinBox->value();
	deviceDetails->safetyStopStartDepth = ui.safetyStopStartDepthDoubleSpinBox->value() * 10;
	deviceDetails->safetyStopEndDepth = ui.safetyStopEndDepthDoubleSpinBox->value() * 10;
	deviceDetails->safetyStopResetDepth = ui.safetyStopResetDepthDoubleSpinBox->value() * 10;

	//set gas values
	gas gas1;
	gas gas2;
	gas gas3;
	gas gas4;
	gas gas5;

	gas1.oxygen = GET_INT_FROM(ui.ostc3GasTable->item(0, 1), 21);
	gas1.helium = GET_INT_FROM(ui.ostc3GasTable->item(0, 2), 0);
	gas1.type = GET_INT_FROM(ui.ostc3GasTable->item(0, 3), 0);
	gas1.depth = GET_INT_FROM(ui.ostc3GasTable->item(0, 4), 0);

	gas2.oxygen = GET_INT_FROM(ui.ostc3GasTable->item(1, 1), 21);
	gas2.helium = GET_INT_FROM(ui.ostc3GasTable->item(1, 2), 0);
	gas2.type = GET_INT_FROM(ui.ostc3GasTable->item(1, 3), 0);
	gas2.depth = GET_INT_FROM(ui.ostc3GasTable->item(1, 4), 0);

	gas3.oxygen = GET_INT_FROM(ui.ostc3GasTable->item(2, 1), 21);
	gas3.helium = GET_INT_FROM(ui.ostc3GasTable->item(2, 2), 0);
	gas3.type = GET_INT_FROM(ui.ostc3GasTable->item(2, 3), 0);
	gas3.depth = GET_INT_FROM(ui.ostc3GasTable->item(2, 4), 0);

	gas4.oxygen = GET_INT_FROM(ui.ostc3GasTable->item(3, 1), 21);
	gas4.helium = GET_INT_FROM(ui.ostc3GasTable->item(3, 2), 0);
	gas4.type = GET_INT_FROM(ui.ostc3GasTable->item(3, 3), 0);
	gas4.depth = GET_INT_FROM(ui.ostc3GasTable->item(3, 4), 0);

	gas5.oxygen = GET_INT_FROM(ui.ostc3GasTable->item(4, 1), 21);
	gas5.helium = GET_INT_FROM(ui.ostc3GasTable->item(4, 2), 0);
	gas5.type = GET_INT_FROM(ui.ostc3GasTable->item(4, 3), 0);
	gas5.depth = GET_INT_FROM(ui.ostc3GasTable->item(4, 4), 0);

	deviceDetails->gas1 = gas1;
	deviceDetails->gas2 = gas2;
	deviceDetails->gas3 = gas3;
	deviceDetails->gas4 = gas4;
	deviceDetails->gas5 = gas5;

	//set dil values
	gas dil1;
	gas dil2;
	gas dil3;
	gas dil4;
	gas dil5;

	dil1.oxygen = GET_INT_FROM(ui.ostc3DilTable->item(0, 1), 21);
	dil1.helium = GET_INT_FROM(ui.ostc3DilTable->item(0, 2), 0);
	dil1.type = GET_INT_FROM(ui.ostc3DilTable->item(0, 3), 0);
	dil1.depth = GET_INT_FROM(ui.ostc3DilTable->item(0, 4), 0);

	dil2.oxygen = GET_INT_FROM(ui.ostc3DilTable->item(1, 1), 21);
	dil2.helium = GET_INT_FROM(ui.ostc3DilTable->item(1, 2), 0);
	dil2.type = GET_INT_FROM(ui.ostc3DilTable->item(1, 3), 0);
	dil2.depth = GET_INT_FROM(ui.ostc3DilTable->item(1, 4), 0);

	dil3.oxygen = GET_INT_FROM(ui.ostc3DilTable->item(2, 1), 21);
	dil3.helium = GET_INT_FROM(ui.ostc3DilTable->item(2, 2), 0);
	dil3.type = GET_INT_FROM(ui.ostc3DilTable->item(2, 3), 0);
	dil3.depth = GET_INT_FROM(ui.ostc3DilTable->item(2, 4), 0);

	dil4.oxygen = GET_INT_FROM(ui.ostc3DilTable->item(3, 1), 21);
	dil4.helium = GET_INT_FROM(ui.ostc3DilTable->item(3, 2), 0);
	dil4.type = GET_INT_FROM(ui.ostc3DilTable->item(3, 3), 0);
	dil4.depth = GET_INT_FROM(ui.ostc3DilTable->item(3, 4), 0);

	dil5.oxygen = GET_INT_FROM(ui.ostc3DilTable->item(4, 1), 21);
	dil5.helium = GET_INT_FROM(ui.ostc3DilTable->item(4, 2), 0);
	dil5.type = GET_INT_FROM(ui.ostc3DilTable->item(4, 3), 0);
	dil5.depth = GET_INT_FROM(ui.ostc3DilTable->item(4, 4), 0);

	deviceDetails->dil1 = dil1;
	deviceDetails->dil2 = dil2;
	deviceDetails->dil3 = dil3;
	deviceDetails->dil4 = dil4;
	deviceDetails->dil5 = dil5;

	//set set point details
	setpoint sp1;
	setpoint sp2;
	setpoint sp3;
	setpoint sp4;
	setpoint sp5;

	sp1.sp = GET_INT_FROM(ui.ostc3SetPointTable->item(0, 1), 70);
	sp1.depth = GET_INT_FROM(ui.ostc3SetPointTable->item(0, 2), 0);

	sp2.sp = GET_INT_FROM(ui.ostc3SetPointTable->item(1, 1), 90);
	sp2.depth = GET_INT_FROM(ui.ostc3SetPointTable->item(1, 2), 20);

	sp3.sp = GET_INT_FROM(ui.ostc3SetPointTable->item(2, 1), 100);
	sp3.depth = GET_INT_FROM(ui.ostc3SetPointTable->item(2, 2), 33);

	sp4.sp = GET_INT_FROM(ui.ostc3SetPointTable->item(3, 1), 120);
	sp4.depth = GET_INT_FROM(ui.ostc3SetPointTable->item(3, 2), 50);

	sp5.sp = GET_INT_FROM(ui.ostc3SetPointTable->item(4, 1), 140);
	sp5.depth = GET_INT_FROM(ui.ostc3SetPointTable->item(4, 2), 70);

	deviceDetails->sp1 = sp1;
	deviceDetails->sp2 = sp2;
	deviceDetails->sp3 = sp3;
	deviceDetails->sp4 = sp4;
	deviceDetails->sp5 = sp5;
}

void ConfigureDiveComputerDialog::populateDeviceDetailsOSTC()
{
	deviceDetails->customText = ui.customTextLlineEdit_3->text();
	deviceDetails->saturation = ui.saturationSpinBox_3->value();
	deviceDetails->desaturation = ui.desaturationSpinBox_3->value();
	deviceDetails->lastDeco = ui.lastDecoSpinBox_3->value();
	deviceDetails->samplingRate = ui.samplingRateSpinBox_3->value();
	deviceDetails->salinity = ui.salinityDoubleSpinBox_3->value() * 100;
	deviceDetails->dateFormat = ui.dateFormatComboBox_3->currentIndex();
	deviceDetails->syncTime = ui.dateTimeSyncCheckBox_3->isChecked();
	deviceDetails->safetyStop = ui.safetyStopCheckBox_3->isChecked();
	deviceDetails->gfHigh = ui.gfHighSpinBox_3->value();
	deviceDetails->gfLow = ui.gfLowSpinBox_3->value();
	deviceDetails->ppO2Min = ui.ppO2MinSpinBox_3->value();
	deviceDetails->ppO2Max = ui.ppO2MaxSpinBox_3->value();
	deviceDetails->futureTTS = ui.futureTTSSpinBox_3->value();
	deviceDetails->decoType = ui.decoTypeComboBox_3->currentIndex();
	deviceDetails->aGFSelectable = ui.aGFSelectableCheckBox_3->isChecked();
	deviceDetails->aGFHigh = ui.aGFHighSpinBox_3->value();
	deviceDetails->aGFLow = ui.aGFLowSpinBox_3->value();
	deviceDetails->bottomGasConsumption = ui.bottomGasConsumption_3->value();
	deviceDetails->decoGasConsumption = ui.decoGasConsumption_3->value();
	deviceDetails->graphicalSpeedIndicator = ui.graphicalSpeedIndicator_3->isChecked();
	deviceDetails->safetyStopLength = ui.safetyStopLengthSpinBox_3->value();
	deviceDetails->safetyStopStartDepth = ui.safetyStopStartDepthDoubleSpinBox_3->value() * 10;
	deviceDetails->safetyStopEndDepth = ui.safetyStopEndDepthDoubleSpinBox_3->value() * 10;
	deviceDetails->safetyStopResetDepth = ui.safetyStopResetDepthDoubleSpinBox_3->value() * 10;

	//set gas values
	gas gas1;
	gas gas2;
	gas gas3;
	gas gas4;
	gas gas5;

	gas1.oxygen = GET_INT_FROM(ui.ostcGasTable->item(0, 1), 21);
	gas1.helium = GET_INT_FROM(ui.ostcGasTable->item(0, 2), 0);
	gas1.type = GET_INT_FROM(ui.ostcGasTable->item(0, 3), 0);
	gas1.depth = GET_INT_FROM(ui.ostcGasTable->item(0, 4), 0);

	gas2.oxygen = GET_INT_FROM(ui.ostcGasTable->item(1, 1), 21);
	gas2.helium = GET_INT_FROM(ui.ostcGasTable->item(1, 2), 0);
	gas2.type = GET_INT_FROM(ui.ostcGasTable->item(1, 3), 0);
	gas2.depth = GET_INT_FROM(ui.ostcGasTable->item(1, 4), 0);

	gas3.oxygen = GET_INT_FROM(ui.ostcGasTable->item(2, 1), 21);
	gas3.helium = GET_INT_FROM(ui.ostcGasTable->item(2, 2), 0);
	gas3.type = GET_INT_FROM(ui.ostcGasTable->item(2, 3), 0);
	gas3.depth = GET_INT_FROM(ui.ostcGasTable->item(2, 4), 0);

	gas4.oxygen = GET_INT_FROM(ui.ostcGasTable->item(3, 1), 21);
	gas4.helium = GET_INT_FROM(ui.ostcGasTable->item(3, 2), 0);
	gas4.type = GET_INT_FROM(ui.ostcGasTable->item(3, 3), 0);
	gas4.depth = GET_INT_FROM(ui.ostcGasTable->item(3, 4), 0);

	gas5.oxygen = GET_INT_FROM(ui.ostcGasTable->item(4, 1), 21);
	gas5.helium = GET_INT_FROM(ui.ostcGasTable->item(4, 2), 0);
	gas5.type = GET_INT_FROM(ui.ostcGasTable->item(4, 3), 0);
	gas5.depth = GET_INT_FROM(ui.ostcGasTable->item(4, 4), 0);

	deviceDetails->gas1 = gas1;
	deviceDetails->gas2 = gas2;
	deviceDetails->gas3 = gas3;
	deviceDetails->gas4 = gas4;
	deviceDetails->gas5 = gas5;

	//set dil values
	gas dil1;
	gas dil2;
	gas dil3;
	gas dil4;
	gas dil5;

	dil1.oxygen = GET_INT_FROM(ui.ostcDilTable->item(0, 1), 21);
	dil1.helium = GET_INT_FROM(ui.ostcDilTable->item(0, 2), 0);
	dil1.type = GET_INT_FROM(ui.ostcDilTable->item(0, 3), 0);
	dil1.depth = GET_INT_FROM(ui.ostcDilTable->item(0, 4), 0);

	dil2.oxygen = GET_INT_FROM(ui.ostcDilTable->item(1, 1), 21);
	dil2.helium = GET_INT_FROM(ui.ostcDilTable->item(1, 2), 0);
	dil2.type = GET_INT_FROM(ui.ostcDilTable->item(1, 3), 0);
	dil2.depth = GET_INT_FROM(ui.ostcDilTable->item(1, 4), 0);

	dil3.oxygen = GET_INT_FROM(ui.ostcDilTable->item(2, 1), 21);
	dil3.helium = GET_INT_FROM(ui.ostcDilTable->item(2, 2), 0);
	dil3.type = GET_INT_FROM(ui.ostcDilTable->item(2, 3), 0);
	dil3.depth = GET_INT_FROM(ui.ostcDilTable->item(2, 4), 0);

	dil4.oxygen = GET_INT_FROM(ui.ostcDilTable->item(3, 1), 21);
	dil4.helium = GET_INT_FROM(ui.ostcDilTable->item(3, 2), 0);
	dil4.type = GET_INT_FROM(ui.ostcDilTable->item(3, 3), 0);
	dil4.depth = GET_INT_FROM(ui.ostcDilTable->item(3, 4), 0);

	dil5.oxygen = GET_INT_FROM(ui.ostcDilTable->item(4, 1), 21);
	dil5.helium = GET_INT_FROM(ui.ostcDilTable->item(4, 2), 0);
	dil5.type = GET_INT_FROM(ui.ostcDilTable->item(4, 3), 0);
	dil5.depth = GET_INT_FROM(ui.ostcDilTable->item(4, 4), 0);

	deviceDetails->dil1 = dil1;
	deviceDetails->dil2 = dil2;
	deviceDetails->dil3 = dil3;
	deviceDetails->dil4 = dil4;
	deviceDetails->dil5 = dil5;

	//set set point details
	setpoint sp1;
	setpoint sp2;
	setpoint sp3;
	setpoint sp4;
	setpoint sp5;

	sp1.sp = GET_INT_FROM(ui.ostcSetPointTable->item(0, 1), 70);
	sp1.depth = GET_INT_FROM(ui.ostcSetPointTable->item(0, 2), 0);

	sp2.sp = GET_INT_FROM(ui.ostcSetPointTable->item(1, 1), 90);
	sp2.depth = GET_INT_FROM(ui.ostcSetPointTable->item(1, 2), 20);

	sp3.sp = GET_INT_FROM(ui.ostcSetPointTable->item(2, 1), 100);
	sp3.depth = GET_INT_FROM(ui.ostcSetPointTable->item(2, 2), 33);

	sp4.sp = GET_INT_FROM(ui.ostcSetPointTable->item(3, 1), 120);
	sp4.depth = GET_INT_FROM(ui.ostcSetPointTable->item(3, 2), 50);

	sp5.sp = GET_INT_FROM(ui.ostcSetPointTable->item(4, 1), 140);
	sp5.depth = GET_INT_FROM(ui.ostcSetPointTable->item(4, 2), 70);

	deviceDetails->sp1 = sp1;
	deviceDetails->sp2 = sp2;
	deviceDetails->sp3 = sp3;
	deviceDetails->sp4 = sp4;
	deviceDetails->sp5 = sp5;
}

void ConfigureDiveComputerDialog::populateDeviceDetailsSuuntoVyper()
{
	deviceDetails->customText = ui.customTextLlineEdit_1->text();
	deviceDetails->samplingRate = ui.samplingRateComboBox_1->currentIndex() == 3 ? 60 : (ui.samplingRateComboBox_1->currentIndex() + 1) * 10;
	deviceDetails->altitude = ui.altitudeRangeComboBox->currentIndex();
	deviceDetails->personalSafety = ui.personalSafetyComboBox->currentIndex();
	deviceDetails->timeFormat = ui.timeFormatComboBox->currentIndex();
	deviceDetails->units = ui.unitsComboBox_1->currentIndex();
	deviceDetails->diveMode = ui.diveModeComboBox_1->currentIndex();
	deviceDetails->lightEnabled = ui.lightCheckBox->isChecked();
	deviceDetails->light = ui.lightSpinBox->value();
	deviceDetails->alarmDepthEnabled = ui.alarmDepthCheckBox->isChecked();
	deviceDetails->alarmDepth = units_to_depth(ui.alarmDepthDoubleSpinBox->value());
	deviceDetails->alarmTimeEnabled = ui.alarmTimeCheckBox->isChecked();
	deviceDetails->alarmTime = ui.alarmTimeSpinBox->value();
}

void ConfigureDiveComputerDialog::readSettings()
{
	ui.progressBar->setValue(0);
	ui.progressBar->setFormat("%p%");
	ui.progressBar->setTextVisible(true);
	// Fw update is no longer a option, needs to be done on a untouched device
	ui.updateFirmwareButton->setEnabled(false);

	config->readSettings(&device_data);
}

void ConfigureDiveComputerDialog::resetSettings()
{
	ui.progressBar->setValue(0);
	ui.progressBar->setFormat("%p%");
	ui.progressBar->setTextVisible(true);

	config->resetSettings(&device_data);
}

void ConfigureDiveComputerDialog::configMessage(QString msg)
{
	ui.progressBar->setFormat(msg);
}

void ConfigureDiveComputerDialog::configError(QString err)
{
	ui.progressBar->setFormat("Error: " + err);
}

void ConfigureDiveComputerDialog::getDeviceData()
{
	device_data.devname = strdup(ui.device->currentText().toUtf8().data());
	device_data.vendor = strdup(selected_vendor.toUtf8().data());
	device_data.product = strdup(selected_product.toUtf8().data());

	device_data.descriptor = descriptorLookup[selected_vendor + selected_product];
	device_data.deviceid = device_data.diveid = 0;

	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	dc->setDevice(device_data.devname);
}

void ConfigureDiveComputerDialog::on_cancel_clicked()
{
	this->close();
}

void ConfigureDiveComputerDialog::on_saveSettingsPushButton_clicked()
{
	ui.progressBar->setValue(0);
	ui.progressBar->setFormat("%p%");
	ui.progressBar->setTextVisible(true);

	populateDeviceDetails();
	config->saveDeviceDetails(deviceDetails, &device_data);
}

void ConfigureDiveComputerDialog::deviceDetailsReceived(DeviceDetails *newDeviceDetails)
{
	deviceDetails = newDeviceDetails;
	reloadValues();
}

void ConfigureDiveComputerDialog::reloadValues()
{
	// Enable the buttons to do operations on this data
	ui.saveSettingsPushButton->setEnabled(true);
	ui.backupButton->setEnabled(true);

	switch (ui.dcStackedWidget->currentIndex()) {
	case 0:
		reloadValuesOSTC3();
		break;
	case 1:
		reloadValuesSuuntoVyper();
		break;
	case 2:
		reloadValuesOSTC();
		break;
	}
}

void ConfigureDiveComputerDialog::reloadValuesOSTC3()
{
	ui.serialNoLineEdit->setText(deviceDetails->serialNo);
	ui.firmwareVersionLineEdit->setText(deviceDetails->firmwareVersion);
	ui.customTextLlineEdit->setText(deviceDetails->customText);
	ui.modelLineEdit->setText(deviceDetails->model);
	ui.diveModeComboBox->setCurrentIndex(deviceDetails->diveMode);
	ui.saturationSpinBox->setValue(deviceDetails->saturation);
	ui.desaturationSpinBox->setValue(deviceDetails->desaturation);
	ui.lastDecoSpinBox->setValue(deviceDetails->lastDeco);
	ui.brightnessComboBox->setCurrentIndex(deviceDetails->brightness);
	ui.unitsComboBox->setCurrentIndex(deviceDetails->units);
	ui.samplingRateComboBox->setCurrentIndex(deviceDetails->samplingRate);
	ui.salinitySpinBox->setValue(deviceDetails->salinity);
	ui.diveModeColour->setCurrentIndex(deviceDetails->diveModeColor);
	ui.languageComboBox->setCurrentIndex(deviceDetails->language);
	ui.dateFormatComboBox->setCurrentIndex(deviceDetails->dateFormat);
	ui.compassGainComboBox->setCurrentIndex(deviceDetails->compassGain);
	ui.safetyStopCheckBox->setChecked(deviceDetails->safetyStop);
	ui.gfHighSpinBox->setValue(deviceDetails->gfHigh);
	ui.gfLowSpinBox->setValue(deviceDetails->gfLow);
	ui.pressureSensorOffsetSpinBox->setValue(deviceDetails->pressureSensorOffset);
	ui.ppO2MinSpinBox->setValue(deviceDetails->ppO2Min);
	ui.ppO2MaxSpinBox->setValue(deviceDetails->ppO2Max);
	ui.futureTTSSpinBox->setValue(deviceDetails->futureTTS);
	ui.ccrModeComboBox->setCurrentIndex(deviceDetails->ccrMode);
	ui.decoTypeComboBox->setCurrentIndex(deviceDetails->decoType);
	ui.aGFSelectableCheckBox->setChecked(deviceDetails->aGFSelectable);
	ui.aGFHighSpinBox->setValue(deviceDetails->aGFHigh);
	ui.aGFLowSpinBox->setValue(deviceDetails->aGFLow);
	ui.calibrationGasSpinBox->setValue(deviceDetails->calibrationGas);
	ui.flipScreenCheckBox->setChecked(deviceDetails->flipScreen);
	ui.setPointFallbackCheckBox->setChecked(deviceDetails->setPointFallback);
	ui.leftButtonSensitivity->setValue(deviceDetails->leftButtonSensitivity);
	ui.rightButtonSensitivity->setValue(deviceDetails->rightButtonSensitivity);
	ui.bottomGasConsumption->setValue(deviceDetails->bottomGasConsumption);
	ui.decoGasConsumption->setValue(deviceDetails->decoGasConsumption);
	ui.modWarning->setChecked(deviceDetails->modWarning);
	ui.dynamicAscendRate->setChecked(deviceDetails->dynamicAscendRate);
	ui.graphicalSpeedIndicator->setChecked(deviceDetails->graphicalSpeedIndicator);
	ui.alwaysShowppO2->setChecked(deviceDetails->alwaysShowppO2);
	ui.tempSensorOffsetDoubleSpinBox->setValue((double)deviceDetails->tempSensorOffset / 10.0);
	ui.safetyStopLengthSpinBox->setValue(deviceDetails->safetyStopLength);
	ui.safetyStopStartDepthDoubleSpinBox->setValue(deviceDetails->safetyStopStartDepth / 10.0);
	ui.safetyStopEndDepthDoubleSpinBox->setValue(deviceDetails->safetyStopEndDepth / 10.0);
	ui.safetyStopResetDepthDoubleSpinBox->setValue(deviceDetails->safetyStopResetDepth / 10.0);

	//load gas 1 values
	ui.ostc3GasTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->gas1.oxygen)));
	ui.ostc3GasTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->gas1.helium)));
	ui.ostc3GasTable->setItem(0, 3, new QTableWidgetItem(QString::number(deviceDetails->gas1.type)));
	ui.ostc3GasTable->setItem(0, 4, new QTableWidgetItem(QString::number(deviceDetails->gas1.depth)));

	//load gas 2 values
	ui.ostc3GasTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->gas2.oxygen)));
	ui.ostc3GasTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->gas2.helium)));
	ui.ostc3GasTable->setItem(1, 3, new QTableWidgetItem(QString::number(deviceDetails->gas2.type)));
	ui.ostc3GasTable->setItem(1, 4, new QTableWidgetItem(QString::number(deviceDetails->gas2.depth)));

	//load gas 3 values
	ui.ostc3GasTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->gas3.oxygen)));
	ui.ostc3GasTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->gas3.helium)));
	ui.ostc3GasTable->setItem(2, 3, new QTableWidgetItem(QString::number(deviceDetails->gas3.type)));
	ui.ostc3GasTable->setItem(2, 4, new QTableWidgetItem(QString::number(deviceDetails->gas3.depth)));

	//load gas 4 values
	ui.ostc3GasTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->gas4.oxygen)));
	ui.ostc3GasTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->gas4.helium)));
	ui.ostc3GasTable->setItem(3, 3, new QTableWidgetItem(QString::number(deviceDetails->gas4.type)));
	ui.ostc3GasTable->setItem(3, 4, new QTableWidgetItem(QString::number(deviceDetails->gas4.depth)));

	//load gas 5 values
	ui.ostc3GasTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->gas5.oxygen)));
	ui.ostc3GasTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->gas5.helium)));
	ui.ostc3GasTable->setItem(4, 3, new QTableWidgetItem(QString::number(deviceDetails->gas5.type)));
	ui.ostc3GasTable->setItem(4, 4, new QTableWidgetItem(QString::number(deviceDetails->gas5.depth)));

	//load dil 1 values
	ui.ostc3DilTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->dil1.oxygen)));
	ui.ostc3DilTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->dil1.helium)));
	ui.ostc3DilTable->setItem(0, 3, new QTableWidgetItem(QString::number(deviceDetails->dil1.type)));
	ui.ostc3DilTable->setItem(0, 4, new QTableWidgetItem(QString::number(deviceDetails->dil1.depth)));

	//load dil 2 values
	ui.ostc3DilTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->dil2.oxygen)));
	ui.ostc3DilTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->dil2.helium)));
	ui.ostc3DilTable->setItem(1, 3, new QTableWidgetItem(QString::number(deviceDetails->dil2.type)));
	ui.ostc3DilTable->setItem(1, 4, new QTableWidgetItem(QString::number(deviceDetails->dil2.depth)));

	//load dil 3 values
	ui.ostc3DilTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->dil3.oxygen)));
	ui.ostc3DilTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->dil3.helium)));
	ui.ostc3DilTable->setItem(2, 3, new QTableWidgetItem(QString::number(deviceDetails->dil3.type)));
	ui.ostc3DilTable->setItem(2, 4, new QTableWidgetItem(QString::number(deviceDetails->dil3.depth)));

	//load dil 4 values
	ui.ostc3DilTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->dil4.oxygen)));
	ui.ostc3DilTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->dil4.helium)));
	ui.ostc3DilTable->setItem(3, 3, new QTableWidgetItem(QString::number(deviceDetails->dil4.type)));
	ui.ostc3DilTable->setItem(3, 4, new QTableWidgetItem(QString::number(deviceDetails->dil4.depth)));

	//load dil 5 values
	ui.ostc3DilTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->dil5.oxygen)));
	ui.ostc3DilTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->dil5.helium)));
	ui.ostc3DilTable->setItem(4, 3, new QTableWidgetItem(QString::number(deviceDetails->dil5.type)));
	ui.ostc3DilTable->setItem(4, 4, new QTableWidgetItem(QString::number(deviceDetails->dil5.depth)));

	//load set point 1 values
	ui.ostc3SetPointTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->sp1.sp)));
	ui.ostc3SetPointTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->sp1.depth)));

	//load set point 2 values
	ui.ostc3SetPointTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->sp2.sp)));
	ui.ostc3SetPointTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->sp2.depth)));

	//load set point 3 values
	ui.ostc3SetPointTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->sp3.sp)));
	ui.ostc3SetPointTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->sp3.depth)));

	//load set point 4 values
	ui.ostc3SetPointTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->sp4.sp)));
	ui.ostc3SetPointTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->sp4.depth)));

	//load set point 5 values
	ui.ostc3SetPointTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->sp5.sp)));
	ui.ostc3SetPointTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->sp5.depth)));
}

void ConfigureDiveComputerDialog::reloadValuesOSTC()
{
	/*
# Not in OSTC
setBrightness
setCalibrationGas
setCompassGain
setDiveMode - Bult into setDecoType
setDiveModeColor - Lots of different colors
setFlipScreen
setLanguage
setPressureSensorOffset
setUnits
setSetPointFallback
setCcrMode
# Not in OSTC3
setNumberOfDives
*/
	ui.serialNoLineEdit_3->setText(deviceDetails->serialNo);
	ui.firmwareVersionLineEdit_3->setText(deviceDetails->firmwareVersion);
	ui.customTextLlineEdit_3->setText(deviceDetails->customText);
	ui.saturationSpinBox_3->setValue(deviceDetails->saturation);
	ui.desaturationSpinBox_3->setValue(deviceDetails->desaturation);
	ui.lastDecoSpinBox_3->setValue(deviceDetails->lastDeco);
	ui.samplingRateSpinBox_3->setValue(deviceDetails->samplingRate);
	ui.salinityDoubleSpinBox_3->setValue((double)deviceDetails->salinity / 100.0);
	ui.dateFormatComboBox_3->setCurrentIndex(deviceDetails->dateFormat);
	ui.safetyStopCheckBox_3->setChecked(deviceDetails->safetyStop);
	ui.gfHighSpinBox_3->setValue(deviceDetails->gfHigh);
	ui.gfLowSpinBox_3->setValue(deviceDetails->gfLow);
	ui.ppO2MinSpinBox_3->setValue(deviceDetails->ppO2Min);
	ui.ppO2MaxSpinBox_3->setValue(deviceDetails->ppO2Max);
	ui.futureTTSSpinBox_3->setValue(deviceDetails->futureTTS);
	ui.decoTypeComboBox_3->setCurrentIndex(deviceDetails->decoType);
	ui.aGFSelectableCheckBox_3->setChecked(deviceDetails->aGFSelectable);
	ui.aGFHighSpinBox_3->setValue(deviceDetails->aGFHigh);
	ui.aGFLowSpinBox_3->setValue(deviceDetails->aGFLow);
	ui.numberOfDivesSpinBox_3->setValue(deviceDetails->numberOfDives);
	ui.bottomGasConsumption_3->setValue(deviceDetails->bottomGasConsumption);
	ui.decoGasConsumption_3->setValue(deviceDetails->decoGasConsumption);
	ui.graphicalSpeedIndicator_3->setChecked(deviceDetails->graphicalSpeedIndicator);
	ui.safetyStopLengthSpinBox_3->setValue(deviceDetails->safetyStopLength);
	ui.safetyStopStartDepthDoubleSpinBox_3->setValue(deviceDetails->safetyStopStartDepth / 10.0);
	ui.safetyStopEndDepthDoubleSpinBox_3->setValue(deviceDetails->safetyStopEndDepth / 10.0);
	ui.safetyStopResetDepthDoubleSpinBox_3->setValue(deviceDetails->safetyStopResetDepth / 10.0);

	//load gas 1 values
	ui.ostcGasTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->gas1.oxygen)));
	ui.ostcGasTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->gas1.helium)));
	ui.ostcGasTable->setItem(0, 3, new QTableWidgetItem(QString::number(deviceDetails->gas1.type)));
	ui.ostcGasTable->setItem(0, 4, new QTableWidgetItem(QString::number(deviceDetails->gas1.depth)));

	//load gas 2 values
	ui.ostcGasTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->gas2.oxygen)));
	ui.ostcGasTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->gas2.helium)));
	ui.ostcGasTable->setItem(1, 3, new QTableWidgetItem(QString::number(deviceDetails->gas2.type)));
	ui.ostcGasTable->setItem(1, 4, new QTableWidgetItem(QString::number(deviceDetails->gas2.depth)));

	//load gas 3 values
	ui.ostcGasTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->gas3.oxygen)));
	ui.ostcGasTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->gas3.helium)));
	ui.ostcGasTable->setItem(2, 3, new QTableWidgetItem(QString::number(deviceDetails->gas3.type)));
	ui.ostcGasTable->setItem(2, 4, new QTableWidgetItem(QString::number(deviceDetails->gas3.depth)));

	//load gas 4 values
	ui.ostcGasTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->gas4.oxygen)));
	ui.ostcGasTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->gas4.helium)));
	ui.ostcGasTable->setItem(3, 3, new QTableWidgetItem(QString::number(deviceDetails->gas4.type)));
	ui.ostcGasTable->setItem(3, 4, new QTableWidgetItem(QString::number(deviceDetails->gas4.depth)));

	//load gas 5 values
	ui.ostcGasTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->gas5.oxygen)));
	ui.ostcGasTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->gas5.helium)));
	ui.ostcGasTable->setItem(4, 3, new QTableWidgetItem(QString::number(deviceDetails->gas5.type)));
	ui.ostcGasTable->setItem(4, 4, new QTableWidgetItem(QString::number(deviceDetails->gas5.depth)));

	//load dil 1 values
	ui.ostcDilTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->dil1.oxygen)));
	ui.ostcDilTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->dil1.helium)));
	ui.ostcDilTable->setItem(0, 3, new QTableWidgetItem(QString::number(deviceDetails->dil1.type)));
	ui.ostcDilTable->setItem(0, 4, new QTableWidgetItem(QString::number(deviceDetails->dil1.depth)));

	//load dil 2 values
	ui.ostcDilTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->dil2.oxygen)));
	ui.ostcDilTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->dil2.helium)));
	ui.ostcDilTable->setItem(1, 3, new QTableWidgetItem(QString::number(deviceDetails->dil2.type)));
	ui.ostcDilTable->setItem(1, 4, new QTableWidgetItem(QString::number(deviceDetails->dil2.depth)));

	//load dil 3 values
	ui.ostcDilTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->dil3.oxygen)));
	ui.ostcDilTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->dil3.helium)));
	ui.ostcDilTable->setItem(2, 3, new QTableWidgetItem(QString::number(deviceDetails->dil3.type)));
	ui.ostcDilTable->setItem(2, 4, new QTableWidgetItem(QString::number(deviceDetails->dil3.depth)));

	//load dil 4 values
	ui.ostcDilTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->dil4.oxygen)));
	ui.ostcDilTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->dil4.helium)));
	ui.ostcDilTable->setItem(3, 3, new QTableWidgetItem(QString::number(deviceDetails->dil4.type)));
	ui.ostcDilTable->setItem(3, 4, new QTableWidgetItem(QString::number(deviceDetails->dil4.depth)));

	//load dil 5 values
	ui.ostcDilTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->dil5.oxygen)));
	ui.ostcDilTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->dil5.helium)));
	ui.ostcDilTable->setItem(4, 3, new QTableWidgetItem(QString::number(deviceDetails->dil5.type)));
	ui.ostcDilTable->setItem(4, 4, new QTableWidgetItem(QString::number(deviceDetails->dil5.depth)));

	//load set point 1 values
	ui.ostcSetPointTable->setItem(0, 1, new QTableWidgetItem(QString::number(deviceDetails->sp1.sp)));
	ui.ostcSetPointTable->setItem(0, 2, new QTableWidgetItem(QString::number(deviceDetails->sp1.depth)));

	//load set point 2 values
	ui.ostcSetPointTable->setItem(1, 1, new QTableWidgetItem(QString::number(deviceDetails->sp2.sp)));
	ui.ostcSetPointTable->setItem(1, 2, new QTableWidgetItem(QString::number(deviceDetails->sp2.depth)));

	//load set point 3 values
	ui.ostcSetPointTable->setItem(2, 1, new QTableWidgetItem(QString::number(deviceDetails->sp3.sp)));
	ui.ostcSetPointTable->setItem(2, 2, new QTableWidgetItem(QString::number(deviceDetails->sp3.depth)));

	//load set point 4 values
	ui.ostcSetPointTable->setItem(3, 1, new QTableWidgetItem(QString::number(deviceDetails->sp4.sp)));
	ui.ostcSetPointTable->setItem(3, 2, new QTableWidgetItem(QString::number(deviceDetails->sp4.depth)));

	//load set point 5 values
	ui.ostcSetPointTable->setItem(4, 1, new QTableWidgetItem(QString::number(deviceDetails->sp5.sp)));
	ui.ostcSetPointTable->setItem(4, 2, new QTableWidgetItem(QString::number(deviceDetails->sp5.depth)));
}

void ConfigureDiveComputerDialog::reloadValuesSuuntoVyper()
{
	const char *depth_unit;
	ui.maxDepthDoubleSpinBox->setValue(get_depth_units(deviceDetails->maxDepth, NULL, &depth_unit));
	ui.maxDepthDoubleSpinBox->setSuffix(depth_unit);
	ui.totalTimeSpinBox->setValue(deviceDetails->totalTime);
	ui.numberOfDivesSpinBox->setValue(deviceDetails->numberOfDives);
	ui.modelLineEdit_1->setText(deviceDetails->model);
	ui.firmwareVersionLineEdit_1->setText(deviceDetails->firmwareVersion);
	ui.serialNoLineEdit_1->setText(deviceDetails->serialNo);
	ui.customTextLlineEdit_1->setText(deviceDetails->customText);
	ui.samplingRateComboBox_1->setCurrentIndex(deviceDetails->samplingRate == 60 ? 3 : (deviceDetails->samplingRate / 10) - 1);
	ui.altitudeRangeComboBox->setCurrentIndex(deviceDetails->altitude);
	ui.personalSafetyComboBox->setCurrentIndex(deviceDetails->personalSafety);
	ui.timeFormatComboBox->setCurrentIndex(deviceDetails->timeFormat);
	ui.unitsComboBox_1->setCurrentIndex(deviceDetails->units);
	ui.diveModeComboBox_1->setCurrentIndex(deviceDetails->diveMode);
	ui.lightCheckBox->setChecked(deviceDetails->lightEnabled);
	ui.lightSpinBox->setValue(deviceDetails->light);
	ui.alarmDepthCheckBox->setChecked(deviceDetails->alarmDepthEnabled);
	ui.alarmDepthDoubleSpinBox->setValue(get_depth_units(deviceDetails->alarmDepth, NULL, &depth_unit));
	ui.alarmDepthDoubleSpinBox->setSuffix(depth_unit);
	ui.alarmTimeCheckBox->setChecked(deviceDetails->alarmTimeEnabled);
	ui.alarmTimeSpinBox->setValue(deviceDetails->alarmTime);
}

void ConfigureDiveComputerDialog::on_backupButton_clicked()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append("Backup.xml");
	QString backupPath = QFileDialog::getSaveFileName(this, tr("Backup dive computer settings"),
							  filename, tr("Backup files (*.xml)"));
	if (!backupPath.isEmpty()) {
		populateDeviceDetails();
		if (!config->saveXMLBackup(backupPath, deviceDetails, &device_data)) {
			QMessageBox::critical(this, tr("XML backup error"),
					      tr("An error occurred while saving the backup file.\n%1")
						      .arg(config->lastError));
		} else {
			QMessageBox::information(this, tr("Backup succeeded"),
						 tr("Your settings have been saved to: %1")
							 .arg(backupPath));
		}
	}
}

void ConfigureDiveComputerDialog::on_restoreBackupButton_clicked()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append("Backup.xml");
	QString restorePath = QFileDialog::getOpenFileName(this, tr("Restore dive computer settings"),
							   filename, tr("Backup files (*.xml)"));
	if (!restorePath.isEmpty()) {
		// Fw update is no longer a option, needs to be done on a untouched device
		ui.updateFirmwareButton->setEnabled(false);
		if (!config->restoreXMLBackup(restorePath, deviceDetails)) {
			QMessageBox::critical(this, tr("XML restore error"),
					      tr("An error occurred while restoring the backup file.\n%1")
						      .arg(config->lastError));
		} else {
			reloadValues();
			QMessageBox::information(this, tr("Restore succeeded"),
						 tr("Your settings have been restored successfully."));
		}
	}
}

void ConfigureDiveComputerDialog::on_updateFirmwareButton_clicked()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath();
	QString firmwarePath = QFileDialog::getOpenFileName(this, tr("Select firmware file"),
							    filename, tr("All files (*.*)"));
	if (!firmwarePath.isEmpty()) {
		ui.progressBar->setValue(0);
		ui.progressBar->setFormat("%p%");
		ui.progressBar->setTextVisible(true);

		config->startFirmwareUpdate(firmwarePath, &device_data);
	}
}


void ConfigureDiveComputerDialog::on_DiveComputerList_currentRowChanged(int currentRow)
{
	switch (currentRow) {
	case 0:
		selected_vendor = "Heinrichs Weikamp";
		selected_product = "OSTC 3";
		fw_upgrade_possible = true;
		break;
	case 1:
		selected_vendor = "Suunto";
		selected_product = "Vyper";
		fw_upgrade_possible = false;
		break;
	case 2:
		selected_vendor = "Heinrichs Weikamp";
		selected_product = "OSTC 2N";
		fw_upgrade_possible = true;
		break;
	default:
		/* Not Supported */
		return;
	}

	int dcType = DC_TYPE_SERIAL;


	if (selected_vendor == QString("Uemis"))
		dcType = DC_TYPE_UEMIS;
	fill_device_list(dcType);
}

void ConfigureDiveComputerDialog::checkLogFile(int state)
{
	ui.chooseLogFile->setEnabled(state == Qt::Checked);
	device_data.libdc_log = (state == Qt::Checked);
	if (state == Qt::Checked && logFile.isEmpty()) {
		pickLogFile();
	}
}

void ConfigureDiveComputerDialog::pickLogFile()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append("subsurface.log");
	logFile = QFileDialog::getSaveFileName(this, tr("Choose file for divecomputer download logfile"),
					       filename, tr("Log files (*.log)"));
	if (!logFile.isEmpty()) {
		free(logfile_name);
		logfile_name = strdup(logFile.toUtf8().data());
	}
}

#ifdef BT_SUPPORT
void ConfigureDiveComputerDialog::selectRemoteBluetoothDevice()
{
	if (!btDeviceSelectionDialog) {
		btDeviceSelectionDialog = new BtDeviceSelectionDialog(this);
		connect(btDeviceSelectionDialog, SIGNAL(finished(int)),
			this, SLOT(bluetoothSelectionDialogIsFinished(int)));
	}

	btDeviceSelectionDialog->show();
}

void ConfigureDiveComputerDialog::bluetoothSelectionDialogIsFinished(int result)
{
	if (result == QDialog::Accepted) {
		ui.device->setCurrentText(btDeviceSelectionDialog->getSelectedDeviceAddress());
		device_data.bluetooth_mode = true;

		ui.progressBar->setFormat("Connecting to device...");
		dc_open();
	}
}
#endif

void ConfigureDiveComputerDialog::dc_open()
{
	getDeviceData();

	QString err = config->dc_open(&device_data);

	if (err != "")
		return ui.progressBar->setFormat(err);

	ui.retrieveDetails->setEnabled(true);
	ui.resetButton->setEnabled(true);
	ui.updateFirmwareButton->setEnabled(true);
	ui.disconnectButton->setEnabled(true);
	ui.restoreBackupButton->setEnabled(true);
	ui.connectButton->setEnabled(false);
	ui.bluetoothMode->setEnabled(false);
	ui.DiveComputerList->setEnabled(false);
	ui.logToFile->setEnabled(false);
	if (fw_upgrade_possible)
		ui.updateFirmwareButton->setEnabled(true);
	ui.progressBar->setFormat("Connected to device");
}

void ConfigureDiveComputerDialog::dc_close()
{
	config->dc_close(&device_data);

	ui.retrieveDetails->setEnabled(false);
	ui.resetButton->setEnabled(false);
	ui.updateFirmwareButton->setEnabled(false);
	ui.disconnectButton->setEnabled(false);
	ui.connectButton->setEnabled(true);
	ui.bluetoothMode->setEnabled(true);
	ui.backupButton->setEnabled(false);
	ui.saveSettingsPushButton->setEnabled(false);
	ui.restoreBackupButton->setEnabled(false);
	ui.DiveComputerList->setEnabled(true);
	ui.logToFile->setEnabled(true);
	ui.updateFirmwareButton->setEnabled(false);
	ui.progressBar->setFormat("Disconnected from device");
	ui.progressBar->setValue(0);
}
