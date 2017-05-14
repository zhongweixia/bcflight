#include <QtWidgets/QGridLayout>
#include <QtGui/QFontDatabase>
#include "MainWindow.h"
#include "ui_window.h"
#include "ui_main.h"
#include "ui_calibrate.h"
#include "ui_camera.h"
#include "ui_network.h"
#include <Controller.h>
#include <links/Socket.h>
#include <links/RawWifi.h>

MainWindow::MainWindow( Controller* controller )
	: QMainWindow()
	, mController( controller )
	, mUpdateTimer( new QTimer( this ) )
{
	for ( uint32_t i = 0; i < 4; i++ ) {
		mCalibrationValues[i].min = 65535;
		mCalibrationValues[i].center = 0;
		mCalibrationValues[i].max = 0;
	}

	ui = new Ui::MainWindow;
	ui->setupUi(this);
	ui->gridLayout_2->setMargin( 0 );
	ui->gridLayout_2->setSpacing( 0 );
	ui->centralLayout->setMargin( 0 );
	ui->centralLayout->setSpacing( 0 );
	connect( ui->btnHome, SIGNAL( clicked() ), this, SLOT( Home() ) );
	connect( ui->btnCalibrate, SIGNAL( clicked() ), this, SLOT( Calibrate() ) );
	connect( ui->btnNetwork, SIGNAL( clicked() ), this, SLOT( Network() ) );
	connect( ui->btnCamera, SIGNAL( clicked() ), this, SLOT( Camera() ) );
	connect( ui->btnSettings, SIGNAL( clicked() ), this, SLOT( Settings() ) );

	uiPageMain = new Ui::PageMain;
	mPageMain = new QWidget();
	uiPageMain->setupUi( mPageMain );
	connect( uiPageMain->calibrate_gyro, SIGNAL( clicked() ), this, SLOT( CalibrateGyro() ) );
	connect( uiPageMain->calibrate_imu, SIGNAL( clicked() ), this, SLOT( CalibrateIMU() ) );
	connect( uiPageMain->reset_battery, SIGNAL( clicked() ), this, SLOT( ResetBattery() ) );

	uiPageCalibrate = new Ui::PageCalibrate;
	mPageCalibrate = new QWidget();
	uiPageCalibrate->setupUi( mPageCalibrate );
	connect( uiPageCalibrate->btnReset, SIGNAL( clicked() ), this, SLOT( CalibrationReset() ) );
	connect( uiPageCalibrate->btnApply, SIGNAL( clicked() ), this, SLOT( CalibrationApply() ) );

	uiPageCamera = new Ui::PageCamera;
	mPageCamera = new QWidget();
	uiPageCamera->setupUi( mPageCamera );
	connect( uiPageCamera->brightness_dec, SIGNAL( clicked() ), this, SLOT( VideoBrightnessDecrease() ) );
	connect( uiPageCamera->brightness_inc, SIGNAL( clicked() ), this, SLOT( VideoBrightnessIncrease() ) );
	connect( uiPageCamera->contrast_dec, SIGNAL( clicked() ), this, SLOT( VideoContrastDecrease() ) );
	connect( uiPageCamera->contrast_inc, SIGNAL( clicked() ), this, SLOT( VideoContrastIncrease() ) );
	connect( uiPageCamera->saturation_dec, SIGNAL( clicked() ), this, SLOT( VideoSaturationDecrease() ) );
	connect( uiPageCamera->saturation_inc, SIGNAL( clicked() ), this, SLOT( VideoSaturationIncrease() ) );
	connect( uiPageCamera->white_balance, SIGNAL( clicked() ), this, SLOT( VideoWhiteBalance() ) );

	uiPageNetwork = new Ui::PageNetwork;
	mPageNetwork = new QWidget();
	uiPageNetwork->setupUi( mPageNetwork );
#ifdef NO_RAWWIFI
	uiPageNetwork->btnRawWifi->setVisible( false );
#else
	if ( static_cast<RawWifi*>(mController->link()) != nullptr ) {
		uiPageNetwork->btnRawWifi->setChecked( true );
	} else
#endif
	if ( static_cast<Socket*>(mController->link()) != nullptr ) {
		uiPageNetwork->btnSocket->setChecked( true );
	} 

	int id = QFontDatabase::addApplicationFont( "data/FreeMonoBold.ttf" );
	QString family = QFontDatabase::applicationFontFamilies(id).at(0);
	QFont mono( family, 14 );
	mono.setBold( true );
	QFont mono_medium( family, 13 );
	mono_medium.setBold( true );
	QFont mono_small( family, 12 );

	setFont( mono );
// 	mPageMain->setFont( mono );
	uiPageMain->status->setFont( mono_small );
	uiPageMain->warnings->setFont( mono_medium );

	ui->btnHome->setChecked( true );
	ui->centralLayout->addWidget( mPageMain );

	connect( mUpdateTimer, SIGNAL( timeout() ), this, SLOT( Update() ) );
	mUpdateTimer->start( 1000 / 10 );

	QTimer* fullRepaintTimer = new QTimer( this );
	connect( fullRepaintTimer, SIGNAL( timeout() ), this, SLOT( repaint() ) );
	fullRepaintTimer->start( 1000 );
}


MainWindow::~MainWindow()
{
	delete ui;
}


void MainWindow::Update()
{
	if ( ui->btnHome->isChecked() ) {
		std::string status = "";
		std::string warnings = "";
		if ( not mController->isConnected() ) {
			status = "Disconnected";
		} else {
			if ( mController->calibrating() ) {
				status += "Calibrating...\n";
			} else if ( not mController->calibrated() ) {
				status += "Gyro calibration needed\n";
			}
			if ( status == "" ) {
				status = "Ready\n";
			}
			if ( mController->mode() == Controller::Stabilize ) {
				status += "Stabilized mode\n";
			} else if ( mController->mode() == Controller::Rate ) {
				status += "Accro mode\n";
			}
			if ( mController->armed() ) {
				status += "Armed\n";
			} else {
				status += "Disarmed\n";
			}
			if ( mController->batteryLevel() < 0.15f ) {
				warnings += "LOW DRONE BATTERY\n";
			}
			if ( mController->localBatteryVoltage() < 0.15f ) {
				warnings += "LOW CONTROLLER BATTERY\n";
			}
			if ( mController->CPUTemp() > 80 ) {
				warnings += "HIGH CPU TEMP (" + std::to_string(mController->CPUTemp()) + "°C)\n";
			}
			if ( mController->cameraMissing() ) {
				warnings += "CAMERA MISSING\n";
			}
		}
		uiPageMain->status->setText( QString::fromStdString( status ) );
		uiPageMain->warnings->setText( QString::fromStdString( warnings ) );
		uiPageMain->localVoltage->setText( QString("%1 V").arg( (double)mController->localBatteryVoltage(), 5, 'f', 2, '0' ) );
		uiPageMain->voltage->setText( QString("%1 V").arg( (double)mController->batteryVoltage(), 5, 'f', 2, '0' ) );
		uiPageMain->totalCurrent->setText( QString("%1 mAh").arg( mController->totalCurrent() ) );
		uiPageMain->latency->setText( QString("%1 ms").arg( mController->ping() ) );
		if ( mController->link()->RxQuality() > 0 ) {
			uiPageMain->txquality->setText( QString("%1% TX").arg( mController->droneRxQuality() ) );
		} else {
			uiPageMain->txquality->setText( QString("??% TX") );
		}
		uiPageMain->txlevel->setText( QString("%1 dBm").arg( mController->droneRxLevel() ) );
		uiPageMain->rxquality->setText( QString("%1% RX").arg( mController->link()->RxQuality() ) );
		uiPageMain->rxlevel->setText( QString("%1 dBm").arg( mController->link()->RxLevel() ) );
	} else if ( ui->btnCalibrate->isChecked() ) {
		int idx = -1;
		uint16_t value = 0;
		if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Thrust" ) {
			idx = 0;
			value = mController->rawThrust();
		} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Yaw" ) {
			idx = 1;
			value = mController->rawYaw();
		} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Pitch" ) {
			idx = 2;
			value = mController->rawPitch();
		} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Roll" ) {
			idx = 3;
			value = mController->rawRoll();
		}
		if ( idx >= 0 and value != 0 ) {
			mCalibrationValues[idx].min = std::min( mCalibrationValues[idx].min, value );
			mCalibrationValues[idx].center =  value;
			mCalibrationValues[idx].max = std::max( mCalibrationValues[idx].max, value );
			uiPageCalibrate->min->setText( QString( "Min : %1").arg( mCalibrationValues[idx].min ) );
			uiPageCalibrate->center->setText( QString( "Center : %1").arg( mCalibrationValues[idx].center ) );
			uiPageCalibrate->max->setText( QString( "Max : %1").arg( mCalibrationValues[idx].max ) );
		}
	}
}


void MainWindow::Home()
{
	ui->btnHome->setChecked( true );
	ui->btnCalibrate->setChecked( false );
	ui->btnNetwork->setChecked( false );
	ui->btnCamera->setChecked( false );
	ui->btnSettings->setChecked( false );
	mPageCalibrate->setVisible( false );
	mPageCamera->setVisible( false );
	mPageNetwork->setVisible( false );
	mPageMain->setVisible( true );
	ui->centralLayout->removeWidget( mPageCalibrate );
	ui->centralLayout->removeWidget( mPageCamera );
	ui->centralLayout->removeWidget( mPageNetwork );
	ui->centralLayout->addWidget( mPageMain );
}


void MainWindow::Calibrate()
{
	mController->Lock();
	ui->btnHome->setChecked( false );
	ui->btnCalibrate->setChecked( true );
	ui->btnNetwork->setChecked( false );
	ui->btnCamera->setChecked( false );
	ui->btnSettings->setChecked( false );
	mPageMain->setVisible( false );
	mPageCamera->setVisible( false );
	mPageNetwork->setVisible( false );
	mPageCalibrate->setVisible( true );
	ui->centralLayout->removeWidget( mPageMain );
	ui->centralLayout->removeWidget( mPageCamera );
	ui->centralLayout->removeWidget( mPageNetwork );
	ui->centralLayout->addWidget( mPageCalibrate );
	mController->Unlock();
}


void MainWindow::Network()
{
	ui->btnHome->setChecked( false );
	ui->btnCalibrate->setChecked( false );
	ui->btnNetwork->setChecked( true );
	ui->btnCamera->setChecked( false );
	ui->btnSettings->setChecked( false );
	mPageMain->setVisible( false );
	mPageCalibrate->setVisible( false );
	mPageCamera->setVisible( false );
	mPageNetwork->setVisible( true );
	ui->centralLayout->removeWidget( mPageMain );
	ui->centralLayout->removeWidget( mPageCalibrate );
	ui->centralLayout->removeWidget( mPageCamera );
	ui->centralLayout->addWidget( mPageNetwork );
}


void MainWindow::Camera()
{
	ui->btnHome->setChecked( false );
	ui->btnCalibrate->setChecked( false );
	ui->btnNetwork->setChecked( false );
	ui->btnCamera->setChecked( true );
	ui->btnSettings->setChecked( false );
	mPageMain->setVisible( false );
	mPageCalibrate->setVisible( false );
	mPageNetwork->setVisible( false );
	mPageCamera->setVisible( true );
	ui->centralLayout->removeWidget( mPageMain );
	ui->centralLayout->removeWidget( mPageCalibrate );
	ui->centralLayout->removeWidget( mPageNetwork );
	ui->centralLayout->addWidget( mPageCamera );
}


void MainWindow::Settings()
{
	ui->btnHome->setChecked( false );
	ui->btnCalibrate->setChecked( false );
	ui->btnNetwork->setChecked( false );
	ui->btnCamera->setChecked( false );
	ui->btnSettings->setChecked( true );
	mPageMain->setVisible( false );
	mPageCalibrate->setVisible( false );
	mPageCamera->setVisible( false );
	mPageNetwork->setVisible( false );
	ui->centralLayout->removeWidget( mPageMain );
	ui->centralLayout->removeWidget( mPageCalibrate );
	ui->centralLayout->removeWidget( mPageCamera );
	ui->centralLayout->removeWidget( mPageNetwork );
}


void MainWindow::CalibrateGyro()
{
	mController->Calibrate();
}


void MainWindow::CalibrateIMU()
{
	mController->CalibrateAll();
}


void MainWindow::ResetBattery()
{
	mController->ResetBattery();
}


void MainWindow::CalibrationReset()
{
	int idx = -1;
	if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Thrust" ) {
		idx = 0;
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Yaw" ) {
		idx = 1;
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Pitch" ) {
		idx = 2;
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Roll" ) {
		idx = 3;
	}
	if ( idx >= 0 ) {
		mCalibrationValues[idx].min = 65535;
		mCalibrationValues[idx].center = 0;
		mCalibrationValues[idx].max = 0;
		uiPageCalibrate->min->setText( QString( "Min : %1").arg( mCalibrationValues[idx].min ) );
		uiPageCalibrate->center->setText( QString( "Center : %1").arg( mCalibrationValues[idx].center ) );
		uiPageCalibrate->max->setText( QString( "Max : %1").arg( mCalibrationValues[idx].max ) );
	}
}


void MainWindow::CalibrationApply()
{
	if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Thrust" ) {
		mController->SaveThrustCalibration( mCalibrationValues[0].min, mCalibrationValues[0].center, mCalibrationValues[0].max );
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Yaw" ) {
		mController->SaveYawCalibration( mCalibrationValues[1].min, mCalibrationValues[1].center, mCalibrationValues[1].max );
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Pitch" ) {
		mController->SavePitchCalibration( mCalibrationValues[2].min, mCalibrationValues[2].center, mCalibrationValues[2].max );
	} else if ( uiPageCalibrate->tabWidget->tabText(uiPageCalibrate->tabWidget->currentIndex()) == "Roll" ) {
		mController->SaveRollCalibration( mCalibrationValues[3].min, mCalibrationValues[3].center, mCalibrationValues[3].max );
	}
}


void MainWindow::VideoBrightnessDecrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoBrightnessDecrease();
	}
}


void MainWindow::VideoBrightnessIncrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoBrightnessIncrease();
	}
}


void MainWindow::VideoContrastDecrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoContrastDecrease();
	}
}


void MainWindow::VideoContrastIncrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoContrastIncrease();
	}
}


void MainWindow::VideoSaturationDecrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoSaturationDecrease();
	}
}


void MainWindow::VideoSaturationIncrease()
{
	if ( mController and mController->isConnected() ) {
		mController->VideoSaturationIncrease();
	}
}


void MainWindow::VideoWhiteBalance()
{
	if ( mController and mController->isConnected() ) {
		uiPageCamera->white_balance->setText( "White balance : " + QString::fromStdString(mController->VideoWhiteBalance()) );
	}
}
