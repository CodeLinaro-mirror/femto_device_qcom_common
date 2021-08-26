#
# This empty Android.mk file exists to prevent the build system from
# automatically including any other Android.mk files under this directory.
#
# system/priv-app/
TARGET_REMOVE_PACKAGES := BackupRestoreConfirmation \
BlockedNumberProvider \
CalendarProvider \
CallLogBackup \
CellBroadcastReceiver \
ContactsProvider \
CtsShimPrivPrebuilt \
DynamicSystemInstallationService \
FusedLocation \
LocalTransport \
ManagedProvisioning \
Mms \
MmsService \
MtpDocumentsProvider \
MusicFX \
ONS \
ProxyHandler \
StatementService \
Tag \
TeleService \
Telecom \
VpnDialogs \
WfdService \
com.qualcomm.location \
xtra_t_app \

# system/app/
TARGET_REMOVE_PACKAGES +=  \
AddWifiConfigStore \
AntHalService \
BATestApp \
BTTestApp \
BasicDreams \
BluetoothExt \
BluetoothMidiService \
BookmarkProvider \
BuiltInPrintService \
CarrierDefaultApp \
CompanionDeviceManager \
CtsShimPrebuilt \
EasterEgg \
FM2 \
HTMLViewer \
KeyChain \
LiveWallpapersPicker \
NQNfcNci \
ODLT \
PacProcessor \
PrintRecommendationService \
Protips \
QColor \
QTIDiagServices \
SecureElement \
SimAppDialog \
SimContact \
SnapdragonSVA \
Stk \
Traceur \
UnifiedSensorTestApp \
UnifiedSensorTestAppService \
WAPPushManager \
WallpaperBackup \
WigigSettings \
workloadclassifier \

# product/app/
TARGET_REMOVE_PACKAGES +=  \
Browser2 \
Calendar \
CallFeaturesSetting \
ConfURIDialer \
ConferenceDialer \
ConnectionManagerTestApp \
DeskClock \
DeviceInfo \
DeviceStatisticsService \
DynamicDDSService \
Email \
ModemTestMode \
PerformanceMode \
PhotoTable \
PowerOffAlarm \
PresenceApp \
PresenceAppSub2 \
QDCMMobileApp \
QdcmFF \
QtiSystemService \
QtiTelephonyService \
QuickSearchBox \
SimSettings \
atfwd \
colorservice \
com.qualcomm.qti.services.secureui \
com.qualcomm.qti.services.systemhelper \
datastatusnotification \
embms \
imssettings \
remoteSimLockAuthentication \
remotesimlockservice \
seccamsample \
uceShimService \
uimgbaservice \
uimlpaservice \
uimremoteclient \
uimremoteserver \
xdivert \

# product/app/
TARGET_REMOVE_PACKAGES += \
CallEnhancement \
CarrierConfig \
Contacts \
Dialer \
EmergencyInfo \
MSDC_UI \
NrNetworkSettingApp \
OneTimeInitializer \
QAS_DVC_MSP \
QtiDialer \
SettingsIntelligence \
WallpaperCropper \
dpmserviceapp \
ims \
seccamservice \

#product/overlay/
TARGET_REMOVE_PACKAGES += \
frameworks-base-overlays \

#vendor/app/
TARGET_REMOVE_PACKAGES +=  \
CACertService \
CneApp \
ConnectionSecurityService \
IWlanService \
QDMA \
QDMA-UI \
QFPCalibration \
QFingerprintService \
SSGTelemetryService \
SSMEditor \
SomeIpService \
TimeService \
TrustZoneAccessService \
com.qualcomm.qti.gpudrivers.sm8250.api29 \
pasrservice \

#vendor/overlay/
TARGET_REMOVE_PACKAGES += \
CarrierConfigResCommon \
CellBroadcastReceiverResCommon \
FrameworksResCommon \
FrameworksResTarget \
OptInAppOverlay \
SystemUIResCommon \
TelecommResCommon \
TelephonyResCommon \
