// SPDX-License-Identifier: GPL-2.0
#include "desktop-widgets/downloadfromdivecomputer.h"
#include "core/helpers.h"
#include "desktop-widgets/mainwindow.h"
#include "desktop-widgets/divelistview.h"
#include "core/display.h"
#include "core/uemis.h"
#include "core/subsurface-qt/SettingsObjectWrapper.h"
#include "qt-models/models.h"
#include "qt-models/diveimportedmodel.h"

#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>

namespace DownloadFromDcGlobal {
	const char *err_string;
};

// Workaround abuse of old libdc types
#define DC_TRANSPORT_BLUETOOTH 1024

DownloadFromDCWidget::DownloadFromDCWidget(QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f),
	downloading(false),
	previousLast(0),
	vendorModel(0),
	productModel(0),
	timer(new QTimer(this)),
	dumpWarningShown(false),
	ostcFirmwareCheck(0),
	currentState(INITIAL)
{
    diveImportedModel = new DiveImportedModel(this);
    diveImportedModel->setDiveTable(&downloadTable);
	vendorModel = new QStringListModel(vendorList);
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);

    int startingWidth = defaultModelFont().pointSize();

	clear_table(&downloadTable);
	ui.setupUi(this);
	ui.progressBar->hide();
	ui.progressBar->setMinimum(0);
	ui.progressBar->setMaximum(100);
	ui.downloadedView->setModel(diveImportedModel);
	ui.downloadedView->setSelectionBehavior(QAbstractItemView::SelectRows);
	ui.downloadedView->setSelectionMode(QAbstractItemView::SingleSelection);
	ui.downloadedView->setColumnWidth(0, startingWidth * 20);
	ui.downloadedView->setColumnWidth(1, startingWidth * 10);
	ui.downloadedView->setColumnWidth(2, startingWidth * 10);
	ui.chooseDumpFile->setEnabled(ui.dumpToFile->isChecked());
	ui.chooseLogFile->setEnabled(ui.logToFile->isChecked());
    ui.selectAllButton->setEnabled(false);
	ui.unselectAllButton->setEnabled(false);
    ui.vendor->setModel(vendorModel);

	progress_bar_text = "";

	timer->setInterval(200);

    connect(ui.downloadedView, SIGNAL(clicked(QModelIndex)), diveImportedModel, SLOT(changeSelected(QModelIndex)));
    connect(ui.chooseDumpFile, SIGNAL(clicked()), this, SLOT(pickDumpFile()));
	connect(ui.dumpToFile, SIGNAL(stateChanged(int)), this, SLOT(checkDumpFile(int)));
	connect(ui.chooseLogFile, SIGNAL(clicked()), this, SLOT(pickLogFile()));
	connect(ui.logToFile, SIGNAL(stateChanged(int)), this, SLOT(checkLogFile(int)));
	connect(ui.selectAllButton, SIGNAL(clicked()), diveImportedModel, SLOT(selectAll()));
	connect(ui.unselectAllButton, SIGNAL(clicked()), diveImportedModel, SLOT(selectNone()));
	connect(timer, SIGNAL(timeout()), this, SLOT(updateProgressBar()));
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
#if defined(BT_SUPPORT) && defined(SSRF_CUSTOM_SERIAL)
    connect(ui.bluetoothMode, SIGNAL(stateChanged(int)), this, SLOT(enableBluetoothMode(int)));
	connect(ui.chooseBluetoothDevice, SIGNAL(clicked()), this, SLOT(selectRemoteBluetoothDevice()));
#endif

	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	if (!dc->dc_vendor().isEmpty()) {
		ui.vendor->setCurrentIndex(ui.vendor->findText(dc->dc_vendor()));
		productModel = new QStringListModel(productList[dc->dc_vendor()]);
		ui.product->setModel(productModel);
		if (!dc->dc_product().isEmpty())
			ui.product->setCurrentIndex(ui.product->findText(dc->dc_product()));
	}
	if (!dc->dc_device().isEmpty())
		ui.device->setEditText(dc->dc_device());

	updateState(INITIAL);
	ui.ok->setEnabled(false);
	ui.downloadCancelRetryButton->setEnabled(true);
	ui.downloadCancelRetryButton->setText(tr("Download"));

#if defined(BT_SUPPORT) && defined(SSRF_CUSTOM_SERIAL)
	ui.bluetoothMode->setText(tr("Choose Bluetooth download mode"));
	ui.bluetoothMode->setChecked(dc->downloadMode() == DC_TRANSPORT_BLUETOOTH);
	btDeviceSelectionDialog = 0;
	ui.chooseBluetoothDevice->setEnabled(ui.bluetoothMode->isChecked());
#else
	ui.bluetoothMode->hide();
	ui.chooseBluetoothDevice->hide();
#endif
}

void DownloadFromDCWidget::updateProgressBar()
{
	static char *last_text = NULL;

	if (same_string(last_text, "")) {
		// if we get the first actual text after the download is finished
		// (which happens for example on the OSTC), then don't bother
		if (!same_string(progress_bar_text, "") && IS_FP_SAME(progress_bar_fraction, 1.0))
			progress_bar_text = NULL;
	}
	if (!same_string(progress_bar_text , "")) {
		ui.progressBar->setFormat(progress_bar_text);
	} else {
		ui.progressBar->setFormat("%p%");
	}
	ui.progressBar->setValue(lrint(progress_bar_fraction * 100));
	free(last_text);
	last_text = strdup(progress_bar_text);
}

void DownloadFromDCWidget::updateState(states state)
{
	if (state == currentState)
		return;

	if (state == INITIAL) {
		fill_device_list(DC_TYPE_OTHER);
		ui.progressBar->hide();
		markChildrenAsEnabled();
		timer->stop();
		progress_bar_text = "";
	}

	// tries to cancel an on going download
	else if (currentState == DOWNLOADING && state == CANCELLING) {
		import_thread_cancelled = true;
		ui.downloadCancelRetryButton->setEnabled(false);
	}

	// user pressed cancel but the application isn't doing anything.
	// means close the window
	else if ((currentState == INITIAL || currentState == DONE || currentState == ERROR) && state == CANCELLING) {
		timer->stop();
		reject();
	}

	// the cancelation process is finished
	else if (currentState == CANCELLING && state == DONE) {
		timer->stop();
		ui.progressBar->setValue(0);
		ui.progressBar->hide();
		markChildrenAsEnabled();
		progress_bar_text = "";
	}

	// DOWNLOAD is finally done, but we don't know if there was an error as libdivecomputer doesn't pass
	// that information on to us.
	// If we find an error, offer to retry, otherwise continue the interaction to pick the dives the user wants
	else if (currentState == DOWNLOADING && state == DONE) {
		timer->stop();
		if (QString(progress_bar_text).contains("error", Qt::CaseInsensitive)) {
			updateProgressBar();
			markChildrenAsEnabled();
			progress_bar_text = "";
		} else {
			progress_bar_text = "";
			ui.progressBar->setValue(100);
			markChildrenAsEnabled();
		}
	}

	// DOWNLOAD is started.
	else if (state == DOWNLOADING) {
		timer->start();
		ui.progressBar->setValue(0);
		progress_bar_fraction = 0.0;
		updateProgressBar();
		ui.progressBar->show();
		markChildrenAsDisabled();
	}

	// got an error
	else if (state == ERROR) {
		timer->stop();
		QMessageBox::critical(this, TITLE_OR_TEXT(tr("Error"), thread.error), QMessageBox::Ok);
		markChildrenAsEnabled();
		progress_bar_text = "";
		ui.progressBar->hide();
	}

	// properly updating the widget state
	currentState = state;
}

void DownloadFromDCWidget::on_vendor_currentIndexChanged(const QString &vendor)
{
	int dcType = DC_TYPE_SERIAL;
	QAbstractItemModel *currentModel = ui.product->model();
	if (!currentModel)
		return;

	productModel = new QStringListModel(productList[vendor]);
	ui.product->setModel(productModel);

	if (vendor == QString("Uemis"))
		dcType = DC_TYPE_UEMIS;
	fill_device_list(dcType);

	// Memleak - but deleting gives me a crash.
	//currentModel->deleteLater();
}

void DownloadFromDCWidget::on_product_currentIndexChanged(const QString &product)
{
	// Set up the DC descriptor
	dc_descriptor_t *descriptor = NULL;
	descriptor = descriptorLookup[ui.vendor->currentText() + product];

	// call dc_descriptor_get_transport to see if the dc_transport_t is DC_TRANSPORT_SERIAL
	if (dc_descriptor_get_transport(descriptor) == DC_TRANSPORT_SERIAL) {
		// if the dc_transport_t is DC_TRANSPORT_SERIAL, then enable the device node box.
		ui.device->setEnabled(true);
	} else {
		// otherwise disable the device node box
		ui.device->setEnabled(false);
	}
}

void DownloadFromDCWidget::on_search_clicked()
{
	if (ui.vendor->currentText() == "Uemis") {
		QString dirName = QFileDialog::getExistingDirectory(this,
								    tr("Find Uemis dive computer"),
								    QDir::homePath(),
								    QFileDialog::ShowDirsOnly);
		if (ui.device->findText(dirName) == -1)
			ui.device->addItem(dirName);
		ui.device->setEditText(dirName);
	}
}

void DownloadFromDCWidget::on_downloadCancelRetryButton_clicked()
{
	if (currentState == DOWNLOADING) {
		updateState(CANCELLING);
		return;
	}
	if (currentState == DONE) {
		// this means we are retrying - so we better clean out the partial
		// list of downloaded dives from the last attempt
		diveImportedModel->clearTable();
		clear_table(&downloadTable);
	}
	updateState(DOWNLOADING);

	// you cannot cancel the dialog, just the download
	ui.cancel->setEnabled(false);
	ui.downloadCancelRetryButton->setText(tr("Cancel download"));

	auto& data = thread.data();
	data.setVendor(ui.vendor->currentText());
	data.setProduct(ui.product->currentText());
#if defined(BT_SUPPORT)
	data.setBluetoothMode(ui.bluetoothMode->isChecked());
	if (data.bluetoothMode() && btDeviceSelectionDialog != NULL) {
		// Get the selected device address
		data.setDevName(btDeviceSelectionDialog->getSelectedDeviceAddress());
	} else
		// this breaks an "else if" across lines... not happy...
#endif
	if (data.vendor() == "Uemis") {
		char *colon;
		char *devname = strdup(ui.device->currentText().toUtf8().data());

		if ((colon = strstr(devname, ":\\ (UEMISSDA)")) != NULL) {
			*(colon + 2) = '\0';
			fprintf(stderr, "shortened devname to \"%s\"", devname);
		}
		data.setDevName(devname);
	} else {
		data.setDevName(ui.device->currentText());
	}
	//TODO: Add the descriptor function.
	// data.descriptor = descriptorLookup[ui.vendor->currentText() + ui.product->currentText()];
	data.setForceDownload(ui.forceDownload->isChecked());
	data.setCreateNewTrip(ui.createNewTrip->isChecked());
	data.setSaveLog(ui.chooseLogFile->isChecked());
	data.setSaveDump(ui.chooseDumpFile->isChecked());

	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	dc->setVendor(data.vendor());
	dc->setProduct(data.product());
	dc->setDevice(data.devName());

#if defined(BT_SUPPORT) && defined(SSRF_CUSTOM_SERIAL)
	dc->setDownloadMode(ui.bluetoothMode->isChecked() ? DC_TRANSPORT_BLUETOOTH : DC_TRANSPORT_SERIAL);
#endif

	connect(&thread, SIGNAL(finished()),
		this, SLOT(onDownloadThreadFinished()), Qt::QueuedConnection);

	//TODO: Don't call mainwindow.
	MainWindow *w = MainWindow::instance();
	connect(&thread, SIGNAL(finished()), w, SLOT(refreshDisplay()));

	// before we start, remember where the dive_table ended
	previousLast = dive_table.nr;

	// TODO: the downloadTable should something inside the thrad or should be something from outside?
	thread.setDiveTable(&downloadTable);
	thread.start();

	// FIXME: We should get the _actual_ device info instead of whatever
	// the user entered in the dropdown.
	// You can enter "OSTC 3" and download just fine from a "OSTC Sport", but
	// this check will compair apples and oranges, firmware wise, then.
	QString product(ui.product->currentText());
	//
	// We shouldn't do this for memory dumps.
	if ((product == "OSTC 3" || product == "OSTC 3+" ||
			product == "OSTC Cr" || product == "OSTC Sport" ||
			product == "OSTC 4") && ! data.saveDump())
	ostcFirmwareCheck = new OstcFirmwareCheck(product);
}

bool DownloadFromDCWidget::preferDownloaded()
{
	return ui.preferDownloaded->isChecked();
}

void DownloadFromDCWidget::checkLogFile(int state)
{
	ui.chooseLogFile->setEnabled(state == Qt::Checked);
	// TODO: Verify the Thread.
	if (state == Qt::Checked && logFile.isEmpty()) {
		pickLogFile();
	}
}

void DownloadFromDCWidget::pickLogFile()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append("subsurface.log");
	logFile = QFileDialog::getSaveFileName(this, tr("Choose file for dive computer download logfile"),
					       filename, tr("Log files (*.log)"));
	if (!logFile.isEmpty()) {
		free(logfile_name);
		logfile_name = strdup(logFile.toUtf8().data());
	}
}

void DownloadFromDCWidget::checkDumpFile(int state)
{
	ui.chooseDumpFile->setEnabled(state == Qt::Checked);
	if (state == Qt::Checked) {
		if (dumpFile.isEmpty())
			pickDumpFile();
		if (!dumpWarningShown) {
			QMessageBox::warning(this, tr("Warning"),
					     tr("Saving the libdivecomputer dump will NOT download dives to the dive list."));
			dumpWarningShown = true;
		}
	}
}

void DownloadFromDCWidget::pickDumpFile()
{
	QString filename = existing_filename ?: prefs.default_filename;
	QFileInfo fi(filename);
	filename = fi.absolutePath().append(QDir::separator()).append("subsurface.bin");
	dumpFile = QFileDialog::getSaveFileName(this, tr("Choose file for dive computer binary dump file"),
						filename, tr("Dump files (*.bin)"));
	if (!dumpFile.isEmpty()) {
		free(dumpfile_name);
		dumpfile_name = strdup(dumpFile.toUtf8().data());
	}
}

void DownloadFromDCWidget::reject()
{
	// we don't want the download window being able to close
	// while we're still downloading.
	if (currentState != DOWNLOADING && currentState != CANCELLING)
		QDialog::reject();
}

void DownloadFromDCWidget::onDownloadThreadFinished()
{
	if (currentState == DOWNLOADING) {
		if (thread.error.isEmpty())
			updateState(DONE);
		else
			updateState(ERROR);
	} else if (currentState == CANCELLING) {
		updateState(DONE);
	}
	ui.downloadCancelRetryButton->setText(tr("Retry download"));
	ui.downloadCancelRetryButton->setEnabled(true);
	// regardless, if we got dives, we should show them to the user
	if (downloadTable.nr) {
		diveImportedModel->setImportedDivesIndexes(0, downloadTable.nr - 1);
	}
}

void DownloadFromDCWidget::on_cancel_clicked()
{
	if (currentState == DOWNLOADING || currentState == CANCELLING)
		return;

	// now discard all the dives
	clear_table(&downloadTable);
	done(-1);
}

void DownloadFromDCWidget::on_ok_clicked()
{
	struct dive *dive;

	if (currentState != DONE && currentState != ERROR)
		return;

	// record all the dives in the 'real' dive_table
	for (int i = 0; i < downloadTable.nr; i++) {
		if (diveImportedModel->data(diveImportedModel->index(i, 0),Qt::CheckStateRole) == Qt::Checked)
			record_dive(downloadTable.dives[i]);
		downloadTable.dives[i] = NULL;
	}
	downloadTable.nr = 0;

	int uniqId, idx;
	// remember the last downloaded dive (on most dive computers this will be the chronologically
	// first new dive) and select it again after processing all the dives
	MainWindow::instance()->dive_list()->unselectDives();

	dive = get_dive(dive_table.nr - 1);
	if (dive != NULL) {
		uniqId = get_dive(dive_table.nr - 1)->id;
		process_dives(true, preferDownloaded());
		// after process_dives does any merging or resorting needed, we need
		// to recreate the model for the dive list so we can select the newest dive
		MainWindow::instance()->recreateDiveList();
		idx = get_idx_by_uniq_id(uniqId);
		// this shouldn't be necessary - but there are reports that somehow existing dives stay selected
		// (but not visible as selected)
		MainWindow::instance()->dive_list()->unselectDives();
		MainWindow::instance()->dive_list()->selectDive(idx, true);
	}

	if (ostcFirmwareCheck && currentState == DONE) {
		ostcFirmwareCheck->checkLatest(this, thread.data().internalData());
	}
	accept();
}

void DownloadFromDCWidget::markChildrenAsDisabled()
{
	ui.device->setEnabled(false);
	ui.vendor->setEnabled(false);
	ui.product->setEnabled(false);
	ui.forceDownload->setEnabled(false);
	ui.createNewTrip->setEnabled(false);
	ui.preferDownloaded->setEnabled(false);
	ui.ok->setEnabled(false);
	ui.search->setEnabled(false);
	ui.logToFile->setEnabled(false);
	ui.dumpToFile->setEnabled(false);
	ui.chooseLogFile->setEnabled(false);
	ui.chooseDumpFile->setEnabled(false);
	ui.selectAllButton->setEnabled(false);
	ui.unselectAllButton->setEnabled(false);
	ui.bluetoothMode->setEnabled(false);
	ui.chooseBluetoothDevice->setEnabled(false);
}

void DownloadFromDCWidget::markChildrenAsEnabled()
{
	ui.device->setEnabled(true);
	ui.vendor->setEnabled(true);
	ui.product->setEnabled(true);
	ui.forceDownload->setEnabled(true);
	ui.createNewTrip->setEnabled(true);
	ui.preferDownloaded->setEnabled(true);
	ui.ok->setEnabled(true);
	ui.cancel->setEnabled(true);
	ui.search->setEnabled(true);
	ui.logToFile->setEnabled(true);
	ui.dumpToFile->setEnabled(true);
	ui.chooseLogFile->setEnabled(true);
	ui.chooseDumpFile->setEnabled(true);
	ui.selectAllButton->setEnabled(true);
	ui.unselectAllButton->setEnabled(true);
#if defined(BT_SUPPORT)
	ui.bluetoothMode->setEnabled(true);
	ui.chooseBluetoothDevice->setEnabled(true);
#endif
}

#if defined(BT_SUPPORT)
void DownloadFromDCWidget::selectRemoteBluetoothDevice()
{
	if (!btDeviceSelectionDialog) {
		btDeviceSelectionDialog = new BtDeviceSelectionDialog(this);
		connect(btDeviceSelectionDialog, SIGNAL(finished(int)),
			this, SLOT(bluetoothSelectionDialogIsFinished(int)));
	}

	btDeviceSelectionDialog->show();
}

void DownloadFromDCWidget::bluetoothSelectionDialogIsFinished(int result)
{
	if (result == QDialog::Accepted) {
		/* Make the selected Bluetooth device default */
		QString selectedDeviceName = btDeviceSelectionDialog->getSelectedDeviceName();

		if (selectedDeviceName == NULL || selectedDeviceName.isEmpty()) {
			ui.device->setCurrentText(btDeviceSelectionDialog->getSelectedDeviceAddress());
		} else {
			ui.device->setCurrentText(selectedDeviceName);
		}
	} else if (result == QDialog::Rejected){
		/* Disable Bluetooth download mode */
		ui.bluetoothMode->setChecked(false);
	}
}

void DownloadFromDCWidget::enableBluetoothMode(int state)
{
	ui.chooseBluetoothDevice->setEnabled(state == Qt::Checked);

	if (state == Qt::Checked)
		selectRemoteBluetoothDevice();
	else
		ui.device->setCurrentIndex(-1);
}
#endif

static void fillDeviceList(const char *name, void *data)
{
	QComboBox *comboBox = (QComboBox *)data;
	comboBox->addItem(name);
}

void DownloadFromDCWidget::fill_device_list(int dc_type)
{
	int deviceIndex;
	ui.device->clear();
	deviceIndex = enumerate_devices(fillDeviceList, ui.device, dc_type);
	if (deviceIndex >= 0)
		ui.device->setCurrentIndex(deviceIndex);
}

