// SPDX-License-Identifier: GPL-2.0
#ifndef QMLMANAGER_H
#define QMLMANAGER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QScreen>
#include <QElapsedTimer>

#include "core/gpslocation.h"
#include "qt-models/divelistmodel.h"

class QMLManager : public QObject {
	Q_OBJECT
	Q_ENUMS(credentialStatus_t)
	Q_PROPERTY(QString cloudUserName READ cloudUserName WRITE setCloudUserName NOTIFY cloudUserNameChanged)
	Q_PROPERTY(QString cloudPassword READ cloudPassword WRITE setCloudPassword NOTIFY cloudPasswordChanged)
	Q_PROPERTY(QString cloudPin READ cloudPin WRITE setCloudPin NOTIFY cloudPinChanged)
	Q_PROPERTY(QString logText READ logText WRITE setLogText NOTIFY logTextChanged)
	Q_PROPERTY(bool locationServiceEnabled READ locationServiceEnabled WRITE setLocationServiceEnabled NOTIFY locationServiceEnabledChanged)
	Q_PROPERTY(bool locationServiceAvailable READ locationServiceAvailable WRITE setLocationServiceAvailable NOTIFY locationServiceAvailableChanged)
	Q_PROPERTY(int distanceThreshold READ distanceThreshold WRITE setDistanceThreshold NOTIFY distanceThresholdChanged)
	Q_PROPERTY(int timeThreshold READ timeThreshold WRITE setTimeThreshold NOTIFY timeThresholdChanged)
	Q_PROPERTY(bool loadFromCloud READ loadFromCloud WRITE setLoadFromCloud NOTIFY loadFromCloudChanged)
	Q_PROPERTY(QString startPageText READ startPageText WRITE setStartPageText NOTIFY startPageTextChanged)
	Q_PROPERTY(bool verboseEnabled READ verboseEnabled WRITE setVerboseEnabled NOTIFY verboseEnabledChanged)
	Q_PROPERTY(credentialStatus_t credentialStatus READ credentialStatus WRITE setCredentialStatus NOTIFY credentialStatusChanged)
	Q_PROPERTY(credentialStatus_t oldStatus READ oldStatus WRITE setOldStatus NOTIFY oldStatusChanged)
	Q_PROPERTY(int accessingCloud READ accessingCloud WRITE setAccessingCloud NOTIFY accessingCloudChanged)
	Q_PROPERTY(bool syncToCloud READ syncToCloud WRITE setSyncToCloud NOTIFY syncToCloudChanged)
	Q_PROPERTY(int updateSelectedDive READ updateSelectedDive WRITE setUpdateSelectedDive NOTIFY updateSelectedDiveChanged)
	Q_PROPERTY(int selectedDiveTimestamp READ selectedDiveTimestamp WRITE setSelectedDiveTimestamp NOTIFY selectedDiveTimestampChanged)
	Q_PROPERTY(QStringList suitInit READ suitInit CONSTANT)
	Q_PROPERTY(QStringList buddyInit READ buddyInit CONSTANT)
	Q_PROPERTY(QStringList divemasterInit READ divemasterInit CONSTANT)
	Q_PROPERTY(QStringList cylinderInit READ cylinderInit CONSTANT)
	Q_PROPERTY(bool showPin READ showPin WRITE setShowPin NOTIFY showPinChanged)

public:
	QMLManager();
	~QMLManager();

	enum credentialStatus_t {
		INCOMPLETE,
		UNKNOWN,
		INVALID,
		VALID_EMAIL,
		VALID,
		NOCLOUD
	};

	static QMLManager *instance();

	QString cloudUserName() const;
	void setCloudUserName(const QString &cloudUserName);

	QString cloudPassword() const;
	void setCloudPassword(const QString &cloudPassword);

	QString cloudPin() const;
	void setCloudPin(const QString &cloudPin);

	bool locationServiceEnabled() const;
	void setLocationServiceEnabled(bool locationServiceEnable);

	bool locationServiceAvailable() const;
	void setLocationServiceAvailable(bool locationServiceAvailable);

	bool verboseEnabled() const;
	void setVerboseEnabled(bool verboseMode);

	int distanceThreshold() const;
	void setDistanceThreshold(int distance);

	int timeThreshold() const;
	void setTimeThreshold(int time);

	bool loadFromCloud() const;
	void setLoadFromCloud(bool done);
	void syncLoadFromCloud();

	QString startPageText() const;
	void setStartPageText(const QString& text);

	credentialStatus_t credentialStatus() const;
	void setCredentialStatus(const credentialStatus_t value);

	credentialStatus_t oldStatus() const;
	void setOldStatus(const credentialStatus_t value);

	QString logText() const;
	void setLogText(const QString &logText);

	int accessingCloud() const;
	void setAccessingCloud(int status);

	bool syncToCloud() const;
	void setSyncToCloud(bool status);

	int updateSelectedDive() const;
	void setUpdateSelectedDive(int idx);

	int selectedDiveTimestamp() const;
	void setSelectedDiveTimestamp(int when);

	typedef void (QMLManager::*execute_function_type)();
	DiveListSortModel *dlSortModel;

	QStringList suitInit() const;
	QStringList buddyInit() const;
	QStringList divemasterInit() const;
	QStringList cylinderInit() const;
	bool showPin() const;
	void setShowPin(bool enable);
	Q_INVOKABLE QStringList getDCListFromVendor(const QString& vendor);

public slots:
	void applicationStateChanged(Qt::ApplicationState state);
	void savePreferences();
	void saveCloudCredentials();
	void checkCredentialsAndExecute(execute_function_type execute);
	void tryRetrieveDataFromBackend();
	void handleError(QNetworkReply::NetworkError nError);
	void handleSslErrors(const QList<QSslError> &errors);
	void retrieveUserid();
	void loadDivesWithValidCredentials();
	void loadDiveProgress(int percent);
	void provideAuth(QNetworkReply *reply, QAuthenticator *auth);
	void commitChanges(QString diveId, QString date, QString location, QString gps,
			   QString duration, QString depth, QString airtemp,
			   QString watertemp, QString suit, QString buddy,
			   QString diveMaster, QString weight, QString notes, QString startpressure,
			   QString endpressure, QString gasmix, QString cylinder);
	void changesNeedSaving();
	void saveChangesLocal();
	void saveChangesCloud(bool forceRemoteSync);
	void deleteDive(int id);
	bool undoDelete(int id);
	QString addDive();
	void addDiveAborted(int id);
	void applyGpsData();
	void sendGpsData();
	void downloadGpsData();
	void populateGpsData();
	void clearGpsData();
	void finishSetup();
	void openLocalThenRemote(QString url);
	void mergeLocalRepo();
	QString getNumber(const QString& diveId);
	QString getDate(const QString& diveId);
	QString getCurrentPosition();
	QString getVersion() const;
	void deleteGpsFix(quint64 when);
	void revertToNoCloudIfNeeded();
	void consumeFinishedLoad(timestamp_t currentDiveTimestamp);
	void refreshDiveList();
	void screenChanged(QScreen *screen);
	qreal lastDevicePixelRatio();
	void setDevicePixelRatio(qreal dpr, QScreen *screen);
	void appendTextToLog(const QString &newText);
	void quit();
	void hasLocationSourceChanged();

private:
	QString m_cloudUserName;
	QString m_cloudPassword;
	QString m_cloudPin;
	QString m_ssrfGpsWebUserid;
	QString m_startPageText;
	QString m_logText;
	bool m_locationServiceEnabled;
	bool m_locationServiceAvailable;
	bool m_verboseEnabled;
	int m_distanceThreshold;
	int m_timeThreshold;
	GpsLocation *locationProvider;
	bool m_loadFromCloud;
	static QMLManager *m_instance;
	QNetworkReply *reply;
	QNetworkRequest request;
	struct dive *deletedDive;
	struct dive_trip *deletedTrip;
	int m_accessingCloud;
	bool m_syncToCloud;
	int m_updateSelectedDive;
	int m_selectedDiveTimestamp;
	credentialStatus_t m_credentialStatus;
	credentialStatus_t m_oldStatus;
	qreal m_lastDevicePixelRatio;
	QElapsedTimer timer;
	bool alreadySaving;
	bool checkDate(DiveObjectHelper *myDive, struct dive * d, QString date);
	bool checkLocation(DiveObjectHelper *myDive, struct dive *d, QString location, QString gps);
	bool checkDuration(DiveObjectHelper *myDive, struct dive *d, QString duration);
	bool checkDepth(DiveObjectHelper *myDive, struct dive *d, QString depth);
	bool currentGitLocalOnly;
	bool m_showPin;

signals:
	void cloudUserNameChanged();
	void cloudPasswordChanged();
	void cloudPinChanged();
	void locationServiceEnabledChanged();
	void locationServiceAvailableChanged();
	void verboseEnabledChanged();
	void logTextChanged();
	void timeThresholdChanged();
	void distanceThresholdChanged();
	void loadFromCloudChanged();
	void startPageTextChanged();
	void credentialStatusChanged();
	void oldStatusChanged();
	void accessingCloudChanged();
	void syncToCloudChanged();
	void updateSelectedDiveChanged();
	void selectedDiveTimestampChanged();
	void showPinChanged();
	void sendScreenChanged(QScreen *screen);
};

#endif
