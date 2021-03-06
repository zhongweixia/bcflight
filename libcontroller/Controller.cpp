/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <math.h>
//#include <netinet/in.h>
//#include <netinet/tcp.h>
#include <stdio.h>
#include <iostream>
#include "Controller.h"
#include "links/RawWifi.h"


Controller::Controller( Link* link, bool spectate )
	: ControllerBase( link )
	, Thread( "controller-tx" )
	, mPing( 0 )
	, mCalibrated( false )
	, mCalibrating( false )
	, mArmed( false )
	, mTotalCurrent( 0 )
	, mCurrentDraw( 0 )
	, mBatteryVoltage( 0 )
	, mBatteryLevel( 0 )
	, mCPULoad( 0 )
	, mCPUTemp( 0 )
	, mAltitude( 0.0f )
	, mThrust( 0.0f )
	, mControlRPY{ 0.0f, 0.0f, 0.0f }
	, mPIDsLoaded( false )
	, mDroneRxQuality( 0 )
	, mDroneRxLevel( 0 )
	, mNightMode( false )
	, mCameraMissing( false )
	, mSpectate( spectate )
	, mTickBase( Thread::GetTick() )
	, mPingTimer( 0 )
	, mDataTimer( 0 )
	, mMsCounter( 0 )
	, mMsCounter50( 0 )
	, mRequestAck( false )
	, mBoardInfos( "" )
	, mSensorsInfos( "" )
	, mConfigFile( "" )
	, mRecordingsList( "" )
	, mUpdateUploadValid( false )
	, mConfigUploadValid( false )
	, mTicks( 0 )
	, mSwitches{ 0 }
	, mVideoRecording( false )
	, mAcceleration( 0.0f )
	, mLocalBatteryVoltage( 0 )
{
	mMode = Rate;
	memset( mSwitches, 0, sizeof( mSwitches ) );

	memset( &mRollPID, 0, sizeof(mRollPID) );
	memset( &mPitchPID, 0, sizeof(mPitchPID) );
	memset( &mYawPID, 0, sizeof(mYawPID) );
	memset( &mOuterPID, 0, sizeof(mOuterPID) );
	memset( &mHorizonOffset, 0, sizeof(mHorizonOffset) );

	memset( &mControls, 0, sizeof(mControls) );

#ifndef WIN32
	signal( SIGPIPE, SIG_IGN );
#endif
	mRxThread = new HookThread<Controller>( "controller-rx", this, &Controller::RxRun );
	mRxThread->setPriority( 99, 1 );

	if ( spectate ) {
		Start();
		mRxThread->Start();
	} else {
		Start();
	}
}


Controller::~Controller()
{
	Stop();
	mRxThread->Stop();
}


std::string Controller::debugOutput()
{
	std::string ret = "";

	mDebugMutex.lock();
	ret = mDebug;
	mDebug = "";
	mDebugMutex.unlock();

	return ret;
}


bool Controller::run()
{
	uint64_t ticks0 = Thread::GetTick();

	if ( mLockState >= 1 ) {
		mLockState = 2;
		usleep( 1000 * 10 );
		return true;
	}

	if ( not mLink ) {
		usleep( 1000 * 500 );
		return true;
	}

	if ( not mLink->isConnected() ) {
		std::cout << "Connecting...";
		mConnected = ( mLink->Connect() == 0 );
		if ( mConnected ) {
			setPriority( 99, 0 );
			std::cout << "Ok !\n" << std::flush;
// 			uint32_t uid = htonl( 0x12345678 );
// 			mLink->Write( &uid, sizeof( uid ) );
		} else {
			std::cout << "Nope !\n" << std::flush;
			usleep( 1000 * 250 );
		}
		return true;
	}

	if ( mSpectate ) {
		mXferMutex.lock();
		if ( mTxFrame.data().size() > 0 ) {
			std::cout << "Sending " << mTxFrame.data().size()*4 << " bytes\n";
			mLink->Write( &mTxFrame );
		}
		mTxFrame = Packet();
		mXferMutex.unlock();

		mTicks = Thread::GetTick();
		mMsCounter += ( mTicks - ticks0 );
		usleep( 1000 * 1000 / 100 );
		return true;
	}

	uint32_t oldswitch[8];
	memcpy( oldswitch, mSwitches, sizeof(oldswitch) );
	for ( uint32_t i = 0; i < 8; i++ ) {
		bool on = ReadSwitch( i );
		if ( on and not mSwitches[i] ) {
			std::cout << "Switch " << i << " on\n" << std::flush;
		} else if ( not on and mSwitches[i] ) {
			std::cout << "Switch " << i << " off\n" << std::flush;
		}
		mSwitches[i] = on;
	}

	if ( mSwitches[1] and not oldswitch[1] ) {
		VideoTakePicture();
	}

	if ( mSwitches[2] and not mArmed and mCalibrated ) {
		Arm();
	} else if ( not mSwitches[2] and mArmed ) {
		printf( "Disarming...\n" );
		Disarm();
	}
	if ( mSwitches[3] and mMode != Stabilize ) {
		setMode( Stabilize );
	} else if ( not mSwitches[3] and mMode != Rate ) {
		setMode( Rate );
	}
	if ( mSwitches[4] and mNightMode == false ) {
		setNightMode( true );
	} else if ( not mSwitches[4] and mNightMode == true ) {
		setNightMode( false );
	}
	if ( mSwitches[5] and not mVideoRecording ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( VIDEO_START_RECORD );
		mXferMutex.unlock();
	} else if ( not mSwitches[5] and mVideoRecording ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( VIDEO_STOP_RECORD );
		mXferMutex.unlock();
	}

	float f_thrust = ReadThrust();
	float f_yaw = ReadYaw();
	float f_pitch = ReadPitch();
	float f_roll = ReadRoll();
	if ( f_thrust >= 0.0f and f_thrust <= 1.0f ) {
		mControls.thrust = (int8_t)( std::max( 0.0f, std::min( 1.0f, f_thrust ) ) * 127.0f );
	}
	if ( f_yaw >= -1.0f and f_yaw <= 1.0f ) {
		if ( fabsf( f_yaw ) <= 0.05f ) {
			f_yaw = 0.0f;
		}
		mControls.yaw = (int8_t)( std::max( -1.0f, std::min( 1.0f, f_yaw ) ) * 127.0f );
	}
	if ( f_pitch >= -1.0f and f_pitch <= 1.0f ) {
		if ( fabsf( f_pitch ) <= 0.05f ) {
			f_pitch = 0.0f;
		}
		else if ( f_pitch < 0.0f ) {
			f_pitch += 0.05f;
		} else if ( f_pitch > 0.0f ) {
			f_pitch -= 0.05f;
		}
		mControls.pitch = (int8_t)( std::max( -1.0f, std::min( 1.0f, f_pitch ) ) * 127.0f );
	}
	if ( f_roll >= -1.0f and f_roll <= 1.0f ) {
		if ( fabsf( f_roll ) <= 0.05f ) {
			f_roll = 0.0f;
		}
		else if ( f_roll < 0.0f ) {
			f_roll += 0.05f;
		} else if ( f_roll > 0.0f ) {
			f_roll -= 0.05f;
		}
		mControls.roll = (int8_t)( std::max( -1.0f, std::min( 1.0f, f_roll ) ) * 127.0f );
	}
	mTxFrame.WriteU16( CONTROLS );
	mTxFrame.Write( (uint8_t*)&mControls, sizeof(mControls) );

	bool request_ack = false;
	if ( Thread::GetTick() - mPingTimer >= 100 ) {
		request_ack = true;
		uint64_t ticks = Thread::GetTick();
		mXferMutex.lock();
		mTxFrame.WriteU16( PING );
		mTxFrame.WriteU16( (uint16_t)( ticks & 0x0000FFFFL ) );
		mTxFrame.WriteU16( mPing ); // Report current ping
		mXferMutex.unlock();

		if ( not mRxThread->running() ) {
			mRxThread->Start();
			mXferMutex.lock();
			mTxFrame.WriteU16( ROLL_PID_FACTORS );
			mTxFrame.WriteU16( PITCH_PID_FACTORS );
			mTxFrame.WriteU16( YAW_PID_FACTORS );
			mTxFrame.WriteU16( OUTER_PID_FACTORS );
			mTxFrame.WriteU16( HORIZON_OFFSET );
			mXferMutex.unlock();
		}

		if ( mUsername == "" ) {
			mXferMutex.lock();
			mTxFrame.WriteU16( GET_USERNAME );
			mXferMutex.unlock();
		}

		mPingTimer = Thread::GetTick();
	}

	mXferMutex.lock();
	if ( mTxFrame.data().size() > 0 ) {
		mLink->Write( &mTxFrame, mRequestAck );
		mRequestAck = request_ack;
	}
	mTxFrame = Packet();
	mXferMutex.unlock();

	if ( Thread::GetTick() - mTicks < 1000 / 150 ) {
		usleep( 1000 * std::max( 0, 1000 / 150 - (int)( Thread::GetTick() - mTicks ) - 1 ) );
	}
	mTicks = Thread::GetTick();
	mMsCounter += ( mTicks - ticks0 );
	return true;
}


bool Controller::RxRun()
{
	if ( mSpectate and not mLink->isConnected() ) {
		std::cout << "Connecting..." << std::flush;
		mConnected = ( mLink->Connect() == 0 );
		if ( mConnected ) {
			std::cout << "Ok !\n" << std::flush;
		} else {
			std::cout << "Nope !\n" << std::flush;
			usleep( 1000 * 250 );
		}
		return true;
	}

	Packet telemetry;
	if ( mLink->Read( &telemetry ) <= 0 ) {
		usleep( 500 );
		return true;
	}
	Cmd cmd = (Cmd)0;

	while ( telemetry.ReadU16( (uint16_t*)&cmd ) > 0 ) {
// 		std::cout << "Received command : " << mCommandsNames[(cmd)] << "\n";

		switch( cmd ) {
			case UNKNOWN : {
				break;
			}
			case PING : {
				uint32_t ret = telemetry.ReadU16();
				uint32_t reported_ping = telemetry.ReadU16();
				uint32_t curr = (uint32_t)( Thread::GetTick() & 0x0000FFFFL );
				mPing = curr - ret;
				if ( mSpectate ) {
					mPing = reported_ping;
				}
				mConnectionEstablished = true;
				break;
			}
			case STATUS : {
				uint32_t status = 0;
				if ( telemetry.ReadU32( &status ) == sizeof(uint32_t) ) {
					mArmed = status & STATUS_ARMED;
					mCalibrated = status & STATUS_CALIBRATED;
					mCalibrating = status & STATUS_CALIBRATING;
					mNightMode = status & STATUS_NIGHTMODE;
				}
				break;
			}
			case TELEMETRY : {
				Telemetry data;
				memset( &data, 0, sizeof(data) );
				telemetry.Read( (uint8_t*)&data, sizeof(data) );
				mBatteryVoltage = (float)(data.battery_voltage) / 100.0f;
				mTotalCurrent = (float)data.total_current;
				mCurrentDraw = (float)data.current_draw;
				mBatteryLevel = (float)(data.battery_level) / 100.0f;
				mCPULoad = data.cpu_load;
				mCPUTemp = data.cpu_temp;
				mDroneRxQuality = data.rx_quality;
				mDroneRxLevel = data.rx_level;
				break;
			}
			case DEBUG_OUTPUT : {
				mDebugMutex.lock();
				std::string str = telemetry.ReadString();
				std::cout << str << std::flush;
				mDebug += str;
				mDebugMutex.unlock();
				break;
			}
			case CALIBRATE : {
				uint32_t value = telemetry.ReadU32();
				if ( value == 0 ) {
					mCalibrated = true;
					std::cout << "Calibration success\n" << std::flush;
				} else if ( value == 2 ) {
					// This value is regularily sent to tell that the drone is still calibrated
					mCalibrated = true;
				} else if ( value == 3 ) {
					// This value is regularily sent to tell that the drone is not calibrated
					mCalibrated = false;
				} else {
					std::cout << "WARNING : Calibration failed !\n" << std::flush;
				}
				break;
			}
			case CALIBRATING : {
				mCalibrating = telemetry.ReadU32();
				break;
			}
			case ARM : {
				mArmed = telemetry.ReadU32();
				break;
			}
			case DISARM : {
				mArmed = telemetry.ReadU32();
				break;
			}
			case RESET_BATTERY : {
				uint32_t unused = telemetry.ReadU32();
				(void)unused;
				break;
			}

			case GET_BOARD_INFOS : {
				mBoardInfos = telemetry.ReadString();
				break;
			}
			case GET_SENSORS_INFOS : {
				mSensorsInfos = telemetry.ReadString();
				break;
			}
			case GET_CONFIG_FILE : {
				uint32_t crc = telemetry.ReadU32();
				std::string content = telemetry.ReadString();
				if ( crc32( (uint8_t*)content.c_str(), content.length() ) == crc ) {
					mConfigFile = content;
				} else {
					std::cout << "Received broken config flie, retrying...\n" << std::flush;
					mConfigFile = "";
				}
				break;
			}
			case SET_CONFIG_FILE : {
				std::cout << "SET_CONFIG_FILE\n";
				mConfigUploadValid = ( telemetry.ReadU32() == 0 );
				break;
			}
			case UPDATE_UPLOAD_DATA : {
				bool ok = ( telemetry.ReadU32() == 1 );
				std::cout << "UPDATE_UPLOAD_DATA : " << ok << "\n" << std::flush;
				mUpdateUploadValid = ok;
				break;
			}

			case VBAT : {
				mBatteryVoltage = telemetry.ReadFloat();
				break;
			}
			case TOTAL_CURRENT : {
				mTotalCurrent = (uint32_t)( telemetry.ReadFloat() * 1000.0f );
				break;
			}
			case CURRENT_DRAW : {
				mCurrentDraw = telemetry.ReadFloat();
				break;
			}
			case BATTERY_LEVEL : {
				float level = 0.0f;
				if ( telemetry.ReadFloat( &level ) == sizeof(float) ) {
					mBatteryLevel = level;
				}
				break;
			}
			case CPU_LOAD : {
				mCPULoad = telemetry.ReadU32();
				break;
			}
			case CPU_TEMP : {
				mCPUTemp = telemetry.ReadU32();
				break;
			}
			case RX_QUALITY : {
				uint32_t value = 0;
				if ( telemetry.ReadU32( &value ) == sizeof(uint32_t) ) {
					mDroneRxQuality = value;
				}
				break;
			}
			case RX_LEVEL : {
				uint32_t value = 0;
				if ( telemetry.ReadU32( &value ) == sizeof(uint32_t) ) {
					mDroneRxLevel = (int32_t)value;
				}
				break;
			}
			case STABILIZER_FREQUENCY : {
				uint32_t value = 0;
				if ( telemetry.ReadU32( &value ) == sizeof(uint32_t) ) {
					mStabilizerFrequency = value;
				}
				break;
			}
			case MOTORS_SPEED: {
				uint32_t size = telemetry.ReadU32();
				mMotorsSpeed.clear();
				for ( uint32_t i = 0; i < size; i++ ) {
// 					mMotorsSpeed.push_back( telemetry.ReadFloat() );
					(void)telemetry.ReadFloat();
				}
				break;
			}

			case ROLL_PID_FACTORS : {
				mRollPID.x = telemetry.ReadFloat();
				mRollPID.y = telemetry.ReadFloat();
				mRollPID.z = telemetry.ReadFloat();
				mPIDsLoaded = true;
				break;
			}
			case PITCH_PID_FACTORS : {
				mPitchPID.x = telemetry.ReadFloat();
				mPitchPID.y = telemetry.ReadFloat();
				mPitchPID.z = telemetry.ReadFloat();
				mPIDsLoaded = true;
				break;
			}
			case YAW_PID_FACTORS : {
				mYawPID.x = telemetry.ReadFloat();
				mYawPID.y = telemetry.ReadFloat();
				mYawPID.z = telemetry.ReadFloat();
				mPIDsLoaded = true;
				break;
			}
			case OUTER_PID_FACTORS : {
				mOuterPID.x = telemetry.ReadFloat();
				mOuterPID.y = telemetry.ReadFloat();
				mOuterPID.z = telemetry.ReadFloat();
				mPIDsLoaded = true;
				break;
			}
			case HORIZON_OFFSET : {
				mHorizonOffset.x = telemetry.ReadFloat();
				mHorizonOffset.y = telemetry.ReadFloat();
				break;
			}
			case SET_ROLL_PID_P : {
				mRollPID.x = telemetry.ReadFloat();
				break;
			}
			case SET_ROLL_PID_I : {
				mRollPID.y = telemetry.ReadFloat();
				break;
			}
			case SET_ROLL_PID_D : {
				mRollPID.z = telemetry.ReadFloat();
				break;
			}
			case SET_PITCH_PID_P : {
				mPitchPID.x = telemetry.ReadFloat();
				break;
			}
			case SET_PITCH_PID_I : {
				mPitchPID.y = telemetry.ReadFloat();
				break;
			}
			case SET_PITCH_PID_D : {
				mPitchPID.z = telemetry.ReadFloat();
				break;
			}
			case SET_YAW_PID_P : {
				mYawPID.x = telemetry.ReadFloat();
				break;
			}
			case SET_YAW_PID_I : {
				mYawPID.y = telemetry.ReadFloat();
				break;
			}
			case SET_YAW_PID_D : {
				mYawPID.z = telemetry.ReadFloat();
				break;
			}
			case SET_OUTER_PID_P : {
				mOuterPID.x = telemetry.ReadFloat();
				break;
			}
			case SET_OUTER_PID_I : {
				mOuterPID.y = telemetry.ReadFloat();
				break;
			}
			case SET_OUTER_PID_D : {
				mOuterPID.z = telemetry.ReadFloat();
				break;
			}
			case SET_HORIZON_OFFSET : {
				mHorizonOffset.x = telemetry.ReadFloat();
				mHorizonOffset.y = telemetry.ReadFloat();
				break;
			}

			case SET_THRUST : {
				float value = telemetry.ReadFloat();
				if ( mSpectate ) {
					mThrust = value;
				}
				break;
			}

			case ROLL_PITCH_YAW : {
				mRPY.x = telemetry.ReadFloat();
				mRPY.y = telemetry.ReadFloat();
				mRPY.z = telemetry.ReadFloat();
				vec4 rpy;
				rpy.x = mRPY.x;
				rpy.y = mRPY.y;
				rpy.z = mRPY.z;
				rpy.w = (double)( Thread::GetTick() - mTickBase ) / 1000.0;
				mHistoryMutex.lock();
				mRPYHistory.emplace_back( rpy );
				if ( mRPYHistory.size() > 256 ) {
					mRPYHistory.pop_front();
				}
				mHistoryMutex.unlock();
				break;
			}
			case GYRO : {
				vec4 rates;
				rates.x = telemetry.ReadFloat();
				rates.y = telemetry.ReadFloat();
				rates.z = telemetry.ReadFloat();
				rates.w = (double)( Thread::GetTick() - mTickBase ) / 1000.0;
				mHistoryMutex.lock();
				mRatesHistory.emplace_back( rates );
				if ( mRatesHistory.size() > 256 ) {
					mRatesHistory.pop_front();
				}
				mHistoryMutex.unlock();
				break;
			}
			case CURRENT_ACCELERATION : {
				mAcceleration = telemetry.ReadFloat();
				break;
			}
			case ALTITUDE : {
				mHistoryMutex.lock();
				mAltitude = telemetry.ReadFloat();
				mAltitudeHistory.emplace_back( mAltitude );
				if ( mAltitudeHistory.size() > 256 ) {
					mAltitudeHistory.pop_front();
				}
				mHistoryMutex.unlock();
				break;
			}
			case SET_MODE : {
				mMode = (Mode)telemetry.ReadU32();
				break;
			}

			case VIDEO_START_RECORD : {
				mVideoRecording = telemetry.ReadU32();
				break;
			}
			case VIDEO_STOP_RECORD : {
				mVideoRecording = telemetry.ReadU32();
				break;
			}
			case VIDEO_WHITE_BALANCE : {
				mVideoWhiteBalance = telemetry.ReadString();
				break;
			}
			case VIDEO_NIGHT_MODE : {
				uint32_t night = telemetry.ReadU32();
				if ( night != mNightMode ) {
					mNightMode = night;
					std::cout << "Video switched to " << ( mNightMode ? "night" : "day" ) << " mode\n";
				}
				break;
			}
			case GET_RECORDINGS_LIST : {
				uint32_t crc = telemetry.ReadU32();
				std::string content = telemetry.ReadString();
				if ( crc32( (uint8_t*)content.c_str(), content.length() ) == crc ) {
					mRecordingsList = content;
				} else {
					std::cout << "Received broken recordings list, retrying...\n";
					mRecordingsList = "broken";
				}
				break;
			}

			case GET_USERNAME : {
				mUsername = telemetry.ReadString();
				break;
			}

			// Errors
			case ERROR_CAMERA_MISSING : {
				mCameraMissing = true;
				break;
			}

			default :
				std::cout << "WARNING : Received unknown packet (" << (uint32_t)cmd << ") !\n" << std::flush;
				break;
		}
	}

	return true;
}


void Controller::Calibrate()
{
	std::cout << "Controller::Calibrate()\n";

	if ( !mLink ) {
		return;
	}

	struct {
		uint32_t cmd = 0;
		uint32_t full_recalibration = 0;
		float current_altitude = 0.0f;
	} calibrate_cmd;
	calibrate_cmd.cmd = htonl( CALIBRATE );
	mCalibrated = false;
	mCalibrating = false;
	while ( not mCalibrating and not mCalibrated ) {
		mXferMutex.lock();
		mTxFrame.Write( (uint8_t*)&calibrate_cmd, sizeof( calibrate_cmd ) );
		mXferMutex.unlock();
		usleep( 50 * 1000 );
		if ( not mCalibrating and not mCalibrated ) {
			usleep( 1000 * 250 );
		}
	}
}


void Controller::CalibrateAll()
{
	std::cout << "Controller::CalibrateAll()\n";

	if ( !mLink ) {
		return;
	}

	struct {
		uint32_t cmd = 0;
		uint32_t full_recalibration = 1;
		float current_altitude = 0.0f;
	} calibrate_cmd;
	calibrate_cmd.cmd = htonl( CALIBRATE );
	mXferMutex.lock();
	mTxFrame.Write( (uint8_t*)&calibrate_cmd, sizeof( calibrate_cmd ) );
	mXferMutex.unlock();
}


void Controller::CalibrateESCs()
{
	if ( !mLink ) {
		return;
	}

	mXferMutex.lock();
	mTxFrame.WriteU16( CALIBRATE_ESCS );
	mXferMutex.unlock();
}


void Controller::setFullTelemetry( bool fullt )
{
	mXferMutex.lock();
	for ( uint32_t retries = 0; retries < 8; retries++ ) {
		mTxFrame.WriteU16( SET_FULL_TELEMETRY );
		mTxFrame.WriteU32( fullt );
	}
	mXferMutex.unlock();
}


void Controller::Arm()
{
	if ( mCalibrated == false ) {
		return;
	}
	mXferMutex.lock();
	for ( uint32_t retries = 0; retries < 1; retries++ ) {
		mTxFrame.WriteU16( ARM );
	}
	mXferMutex.unlock();
}


void Controller::Disarm()
{
	mXferMutex.lock();
	for ( uint32_t retries = 0; retries < 1; retries++ ) {
		mTxFrame.WriteU16( DISARM );
	}
	mThrust = 0.0f;
	mXferMutex.unlock();
}


void Controller::ResetBattery()
{
	mXferMutex.lock();
	for ( uint32_t retries = 0; retries < 1; retries++ ) {
		mTxFrame.WriteU16( RESET_BATTERY );
		mTxFrame.WriteU32( 0U );
	}
	mXferMutex.unlock();
}


std::string Controller::getBoardInfos()
{
	// Wait for data to be filled by RX Thread (RxRun())
	while ( mBoardInfos.length() == 0 ) {
		mXferMutex.lock();
		for ( uint32_t retries = 0; retries < 1; retries++ ) {
			mTxFrame.WriteU16( GET_BOARD_INFOS );
		}
		mXferMutex.unlock();
		usleep( 1000 * 250 );
	}

	return mBoardInfos;
}


std::string Controller::getSensorsInfos()
{
	// Wait for data to be filled by RX Thread (RxRun())
	while ( mSensorsInfos.length() == 0 ) {
		mXferMutex.lock();
		for ( uint32_t retries = 0; retries < 1; retries++ ) {
			mTxFrame.WriteU16( GET_SENSORS_INFOS );
		}
		mXferMutex.unlock();
		usleep( 1000 * 250 );
	}

	return mSensorsInfos;
}


std::string Controller::getConfigFile()
{
	mConfigFile = "";

	// Wait for data to be filled by RX Thread (RxRun())
	while ( mConfigFile.length() == 0 ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( GET_CONFIG_FILE );
		mXferMutex.unlock();
		usleep( 1000 * 250 );
	}

	return mConfigFile;
}


void Controller::setConfigFile( const std::string& content )
{
	std::cout << "setConfigFile...\n" << std::flush;
	Packet packet( SET_CONFIG_FILE );
	packet.WriteU32( crc32( (uint8_t*)content.c_str(), content.length() ) );
	packet.WriteString( content );

	mConfigUploadValid = false;
	while ( not mConfigUploadValid ) {
		mXferMutex.lock();
// 		mTxFrame.WriteU16( SET_CONFIG_FILE );
// 		mTxFrame.WriteString( content );
		std::cout << "Sending " << packet.data().size()*4 << " bytes\n";
		mLink->Write( &packet );
		mXferMutex.unlock();
		usleep( 1000 * 250 );
	};
	mConfigFile = "";
	std::cout << "setConfigFile ok\n" << std::flush;
}


void Controller::UploadUpdateInit()
{
	Packet init( UPDATE_UPLOAD_INIT );
	for ( uint32_t retries = 0; retries < 8; retries++ ) {
		mXferMutex.lock();
		mLink->Write( &init );
		mXferMutex.unlock();
		usleep( 1000 * 50 );
	}
}


void Controller::UploadUpdateData( const uint8_t* buf, uint32_t offset, uint32_t size )
{

	Packet packet( UPDATE_UPLOAD_DATA );
	packet.WriteU32( crc32( buf, size ) );
	packet.WriteU32( offset );
	packet.WriteU32( offset );
	packet.WriteU32( size );
	packet.WriteU32( size );
	packet.Write( buf, size );


	mUpdateUploadValid = false;
	mXferMutex.lock();
	int retries = 1;
	mLink->retriesCount();
	mLink->setRetriesCount( 1 );
	do {
		mLink->Write( &packet );
		usleep( 1000 * 50 );
		if ( not mUpdateUploadValid ) {
			std::cout << "Broken data received, retrying...\n" << std::flush;
			usleep( 1000 * 10 );
		}
	} while ( not mUpdateUploadValid );

	mLink->setRetriesCount( retries );
	mXferMutex.unlock();

}


void Controller::UploadUpdateProcess( const uint8_t* buf, uint32_t size )
{
	Packet process( UPDATE_UPLOAD_PROCESS );
	process.WriteU32( crc32( buf, size ) );
	for ( uint32_t retries = 0; retries < 8; retries++ ) {
		mXferMutex.lock();
		mLink->Write( &process );
		mXferMutex.unlock();
		usleep( 1000 * 50 );
	}
}


void Controller::EnableTunDevice()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( ENABLE_TUN_DEVICE );
	mXferMutex.unlock();
}


void Controller::DisableTunDevice()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( DISABLE_TUN_DEVICE );
	mXferMutex.unlock();
}


void Controller::ReloadPIDs()
{
	for ( uint32_t retries = 0; retries < 4; retries++ ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( ROLL_PID_FACTORS );
		mTxFrame.WriteU16( PITCH_PID_FACTORS );
		mTxFrame.WriteU16( YAW_PID_FACTORS );
		mTxFrame.WriteU16( OUTER_PID_FACTORS );
		mXferMutex.unlock();
		usleep( 1000 * 10 );
	}
}


void Controller::setRollPID( const vec3& v )
{
	std::cout << "setRollPID...\n" << std::flush;
	while ( mRollPID.x != v.x or mRollPID.y != v.y or mRollPID.z != v.z ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( SET_ROLL_PID_P );
		mTxFrame.WriteFloat( v.x );
		mTxFrame.WriteU16( SET_ROLL_PID_I );
		mTxFrame.WriteFloat( v.y );
		mTxFrame.WriteU16( SET_ROLL_PID_D );
		mTxFrame.WriteFloat( v.z );
		mXferMutex.unlock();
		usleep( 1000 * 100 );
	}
	std::cout << "setRollPID ok\n" << std::flush;
}


void Controller::setPitchPID( const vec3& v )
{
	std::cout << "setPitchPID...\n" << std::flush;
	while ( mPitchPID.x != v.x or mPitchPID.y != v.y or mPitchPID.z != v.z ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( SET_PITCH_PID_P );
		mTxFrame.WriteFloat( v.x );
		mTxFrame.WriteU16( SET_PITCH_PID_I );
		mTxFrame.WriteFloat( v.y );
		mTxFrame.WriteU16( SET_PITCH_PID_D );
		mTxFrame.WriteFloat( v.z );
		mXferMutex.unlock();
		usleep( 1000 * 100 );
	}
	std::cout << "setPitchPID ok\n" << std::flush;
}


void Controller::setYawPID( const vec3& v )
{
	std::cout << "setYawPID...\n" << std::flush;
	while ( mYawPID.x != v.x or mYawPID.y != v.y or mYawPID.z != v.z ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( SET_YAW_PID_P );
		mTxFrame.WriteFloat( v.x );
		mTxFrame.WriteU16( SET_YAW_PID_I );
		mTxFrame.WriteFloat( v.y );
		mTxFrame.WriteU16( SET_YAW_PID_D );
		mTxFrame.WriteFloat( v.z );
		mXferMutex.unlock();
		usleep( 1000 * 100 );
	}
	std::cout << "setYawPID ok\n" << std::flush;
}


void Controller::setOuterPID( const vec3& v )
{
	std::cout << "setOuterPID...\n" << std::flush;
	while ( mOuterPID.x != v.x or mOuterPID.y != v.y or mOuterPID.z != v.z ) {
		mXferMutex.lock();
		mTxFrame.WriteU16( SET_OUTER_PID_P );
		mTxFrame.WriteFloat( v.x );
		mTxFrame.WriteU16( SET_OUTER_PID_I );
		mTxFrame.WriteFloat( v.y );
		mTxFrame.WriteU16( SET_OUTER_PID_D );
		mTxFrame.WriteFloat( v.z );
		mXferMutex.unlock();
		usleep( 1000 * 100 );
	}
	std::cout << "setOuterPID ok\n" << std::flush;
}


void Controller::setHorizonOffset( const vec3& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_HORIZON_OFFSET );
	mTxFrame.WriteFloat( v.x );
	mTxFrame.WriteFloat( v.y );
	mXferMutex.unlock();
}


void Controller::setThrust( const float& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_THRUST );
	mTxFrame.WriteFloat( v );
	mXferMutex.unlock();
	mThrust = v;
}


void Controller::setThrustRelative( const float& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_THRUST );
	mTxFrame.WriteFloat( mThrust + v );
	mThrust += v;
	mXferMutex.unlock();
}


void Controller::setRoll( const float& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_ROLL );
	mTxFrame.WriteFloat( v );
	mControlRPY.x = v;
	mXferMutex.unlock();
}


void Controller::setPitch( const float& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_PITCH );
	mTxFrame.WriteFloat( v );
	mControlRPY.y = v;
	mXferMutex.unlock();
}



void Controller::setYaw( const float& v )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_YAW );
	mTxFrame.WriteFloat( v );
	mControlRPY.z = v;
	mXferMutex.unlock();
}


void Controller::setMode( const Controller::Mode& mode )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( SET_MODE );
	mTxFrame.WriteU32( (uint32_t)mode );
	mXferMutex.unlock();
}


void Controller::VideoPause()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_PAUSE );
	mXferMutex.unlock();
}


void Controller::VideoResume()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_RESUME );
	mXferMutex.unlock();
}


void Controller::VideoTakePicture()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_TAKE_PICTURE );
	mXferMutex.unlock();
}


void Controller::VideoBrightnessIncrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_BRIGHTNESS_INCR );
	mXferMutex.unlock();
}


void Controller::VideoBrightnessDecrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_BRIGHTNESS_DECR );
	mXferMutex.unlock();
}


void Controller::VideoContrastIncrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_CONTRAST_INCR );
	mXferMutex.unlock();
}


void Controller::VideoContrastDecrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_CONTRAST_DECR );
	mXferMutex.unlock();
}


void Controller::VideoSaturationIncrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_SATURATION_INCR );
	mXferMutex.unlock();
}


void Controller::VideoSaturationDecrease()
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_SATURATION_DECR );
	mXferMutex.unlock();
}


std::string Controller::VideoWhiteBalance()
{
	mVideoWhiteBalance = "";

	do {
		mXferMutex.lock();
		mTxFrame.WriteU16( VIDEO_WHITE_BALANCE );
		mXferMutex.unlock();
		usleep( 250 * 1000 );
	} while ( mVideoWhiteBalance == "" );

	return mVideoWhiteBalance;
}


void Controller::setNightMode( const bool& night )
{
	mXferMutex.lock();
	mTxFrame.WriteU16( VIDEO_NIGHT_MODE );
	mTxFrame.WriteU32( night );
	mXferMutex.unlock();
}


std::vector< std::string > Controller::recordingsList()
{
	mRecordingsList = "";

	// Wait for data to be filled by RX Thread (RxRun())
	do {
		mXferMutex.lock();
		mTxFrame.WriteU16( GET_RECORDINGS_LIST );
		mXferMutex.unlock();
		uint32_t retry = 0;
		while ( mRecordingsList.length() == 0 and retry++ < 1000 / 250 ) {
			usleep( 1000 * 250 );
		}
	} while ( mRecordingsList == "broken" );

	std::cout << "mRecordingsList : " << mRecordingsList << "\n";
	std::vector< std::string > list;
	std::string full = mRecordingsList;
	while ( full.length() > 0 ) {
		list.emplace_back( full.substr( 0, full.find( ";" ) ) );
		full = full.substr( full.find( ";" ) + 1 );
	}
	return list;
}


float Controller::acceleration() const
{
	return mAcceleration;
}


std::list< vec4 > Controller::rpyHistory()
{
	mHistoryMutex.lock();
	std::list< vec4 > ret = mRPYHistory;
	mHistoryMutex.unlock();
	return ret;
}


std::list< vec4 > Controller::ratesHistory()
{
	mHistoryMutex.lock();
	std::list< vec4 > ret = mRatesHistory;
	mHistoryMutex.unlock();
	return ret;
}


std::list< vec3 > Controller::outerPidHistory()
{
	mHistoryMutex.lock();
	std::list< vec3 > ret = mOuterPIDHistory;
	mHistoryMutex.unlock();
	return ret;
}


std::list< float > Controller::altitudeHistory()
{
	mHistoryMutex.lock();
	std::list< float > ret = mAltitudeHistory;
	mHistoryMutex.unlock();
	return ret;
}


float Controller::localBatteryVoltage() const
{
	return mLocalBatteryVoltage;
}


uint32_t Controller::crc32( const uint8_t* buf, uint32_t len )
{
	uint32_t k = 0;
	uint32_t crc = 0;

	crc = ~crc;

	while ( len-- ) {
		crc ^= *buf++;
		for ( k = 0; k < 8; k++ ) {
			crc = ( crc & 1 ) ? ( (crc >> 1) ^ 0x82f63b78 ) : ( crc >> 1 );
		}
	}

	return ~crc;
}

void Controller::MotorTest(uint32_t id)
{
	if ( !mLink ) {
		return;
	}

	mXferMutex.lock();
	mTxFrame.WriteU16( MOTOR_TEST );
	mTxFrame.WriteU32( id );
	mXferMutex.unlock();
}

