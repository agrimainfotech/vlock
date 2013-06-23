/* Copyright (c) 2012 Research In Motion Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PostHttp.hpp"
#include <bb/device/DeviceInfo>
#include <bps/deviceinfo.h>
#include "AppSettings.hpp"
#include <sstream>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QUrl>
#include <QString>
#include <QtNetwork>
#include <QImage>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <inttypes.h>
#include <cstring>
#include <deque>
#include <string>
#include <QFile>
#include <QVariant>
#include<bb/cascades/maps/MapView>
#include<QtLocationSubset/qgeopositioninfo>
#include <QtLocationSubset/QGeoPositionInfoSource>
#include<qt4/QtMobility/qmalgorithms.h>
#include<qt4/QtMobility/qmobilityglobal.h>
#include <bps/navigator.h>
#include <bb/device/HardwareInfo>
#include<bb/platform/geo/GeoLocation>
#include<time.h>
#include "EncodeFlac.h"
#include <QApplication>
#include <QDeclarativeEngine>
#include <QDeclarativeComponent>
#include <QDeclarativeItem>
#include <QDebug>

using namespace bb::cascades;
using namespace bb::cascades::maps;
using namespace QtMobilitySubset;
using namespace QtMobility;
using namespace std;
using namespace bb::platform::geo;
using namespace bb::device;

/**
 * PostHttp
 *
 * In this class you will learn the following:
 * -- How to use QNetworkAccessManager to make a network request
 * -- How to set Http headers for your request
 * -- How to setup a secure connection with QSslConfiguration
 * -- How to read a network response with QNetworkReply
 * -- How to parse JSON data using JsonDataAccess
 */
//! [0]

QString tmp1,tmp2,tmp3,tmp0;
QString resp2;
QString gdata;
std::string ReplaceString(std::string subject, const std::string& search,
                         const std::string& replace);
string getBetween(string fullstring, string start, string end);
vector <string> fields;
int is_big_endian(void) {
	union {
		uint32_t i;
		char c[4];
	} bint = { 0x01020304 };

	return bint.c[0] == 1;
}

template<typename T>
T swap_endian(T u) {
	union {
		T u;
		unsigned char u8[sizeof(T)];
	} source, dest;

	source.u = u;

	for (size_t k = 0; k < sizeof(T); k++)
		dest.u8[k] = source.u8[sizeof(T) - k - 1];

	return dest.u;
}

double sinc(double x) {
	if (x == 0.0)
		return 1.0;
	x *= 3.141592653; // pi
	return sin(x) / x;
}

// resample a stereo 16-bit stream to mono 16-bit stream, in_length in bytes
//	resample(ifile, ofile, 48000, 16000, chunk_size);
void resample(ifstream &ifile, ofstream &ofile, unsigned in_rate,
		unsigned out_rate, unsigned in_length) {
	/* out_length is assumed to be ceil(in_length*out_rate/in_rate) */
	double ratio = in_rate / (double) out_rate;
	unsigned out_length = std::ceil(in_length / ratio / 1.0); // st -> mono
	unsigned out_samples = out_length / 2; // 2 bytes per sample

	const double support = 4.0;
	deque<int16_t> sweep; // always 9 items
	int initial_pos = ifile.tellg();
	int16_t cur;
	int16_t output;
	// load fantom values
	for (int j = 0; j < 4; j++) {
		sweep.push_back(0);
	}
	// load first 5 items
	for (int j = 0; j < 5; j++) {
		ifile.read((char *) (&cur), sizeof(cur));
		 sweep.push_back(cur);
	 //	ifile.read((char *) (&cur), sizeof(cur)); //for beta
	}

	for (unsigned o = 0; o < out_samples; ++o) {
		double sum = 0.0;
		double result = 0.0;

		deque<int16_t>::iterator sample = sweep.begin();
		for (int i = 4; i > -5; sample++, i--) {
			double factor = sinc(i);
			result += double((*sample) * factor);
			sum += factor;
		}
		if (sum != 0.0)
			result /= sum;
		if (result <= -32768) {
			output = -32768;
		} else if (result > 32767) {
			output = 32767;
		} else {
			output = (int16_t) (result + 0.5);
		}
		ofile.write((char *) (&output), sizeof(output));
		// load next element
		cur = 0;
		ifile.read((char *) (&cur), sizeof(cur));
		sweep.push_back(cur);
		ifile.read((char *) (&cur), sizeof(cur));
	 	  ifile.read((char *) (&cur), sizeof(cur));//for beta
	 	sweep.push_back(cur);
	 	ifile.read((char *) (&cur), sizeof(cur));
	 	 ifile.read((char *) (&cur), sizeof(cur));//for beta
	 	sweep.push_back(cur);
	 	ifile.read((char *) (&cur), sizeof(cur));//for beta
		// remove first element
		sweep.pop_front();
		sweep.pop_front();
		sweep.pop_front();
	}
}

struct WavHeader {
	char riff[4]; // must contain "RIFF"
	int32_t file_size_8; // file size less 8 bytes
	char format[4]; // must contain "WAVE"

	char fmt[4]; // must contain "fmt "
	int32_t fmt_size_8; // size of the format subchunk
	int16_t audio_format; // 1 for PCM, other for compressed
	int16_t num_channels; // 1 for mono, 2 for stereo
	int32_t sample_rate; // sample rate: 8000, 44100, etc
	int32_t byte_rate; // = sample_rate * num_channels * (bps / 8)
	int16_t block_align; // = num_channels * (bps / 8)
	int16_t bps; // bits per sample: 8, 16 etc.
};

PostHttp::PostHttp(QObject* parent) :
		QObject(parent), m_networkAccessManager(new QNetworkAccessManager(this)) {
}
//! [0]

/**
 * PostHttp::post
 *
 * Make a network request to httpbin.org/post with POST data and get
 * the response
 */
//! [1]
void PostHttp::post() {
	HardwareInfo sd;
	 DeviceInfo *bbdvce = new DeviceInfo();



 cout << "device info:"    << endl;
	cout << "Uploading... "   << endl;
    QGeoPositionInfoSource *src = QGeoPositionInfoSource::createDefaultSource(this);
    src->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
    QGeoPositionInfo lastPosition2 = src->lastKnownPosition(true);
if(src){
	cout << "gps source exist"  <<  endl;
	bool res =connect(src, SIGNAL(positionUpdated(QGeoPositionInfo)), this,
					SLOT(positionUpdated(QGeoPositionInfo)));
	cout << "source connected information : " << res << endl;
	cout << "gps1"  <<  endl;
		//	Q_ASSERT(res);
		//	Q_UNUSED(res);
			cout << "gps2"  <<  endl;
			src->setPreferredPositioningMethods(
			QGeoPositionInfoSource::SatellitePositioningMethods);
			src->setUpdateInterval(1000);
			cout << "gps3"  <<  endl;
	 		src->setProperty("backgroundMode", true);
	                  src->startUpdates();


	        cout << "updated satelite information: " << lastPosition2.coordinate().latitude() << endl;

	        cout << "updated satelite information: " << lastPosition2.coordinate().longitude() << endl;

}
	else
	cout << "gps no source exist" << endl;
cout << "updated satelite information2: " << lastPosition2.coordinate().latitude() << endl;

cout << "updated satelite information2: " << lastPosition2.coordinate().longitude() << endl;

    // Connect the positionUpdated() signal to a
    // slot that handles position updates.
    src->setUpdateInterval(1000);
src->startUpdates();

cout << "supported sources:" << src->preferredPositioningMethods() << endl;

    bool positionUpdatedConnected = connect(src,
        SIGNAL(positionUpdated(const QGeoPositionInfo &)),
        this,
        SLOT(positionUpdated(const QGeoPositionInfo &)));


    if (positionUpdatedConnected) {
        // Signal was successfully connected.
        // Request a single fix and wait for the
        // positionUpdated() signal to be emitted.
        src->requestUpdate();
        src->requestUpdate();
        src->requestUpdate();
    	cout << " Successfully  connected to GPS " << endl ;
    } else {
       // Failed to connect to signal.
   	cout << "Failed to connect GPS " << endl ;
   }
 //   src->requestUpdate();
    QGeoPositionInfo lastPosition = src->lastKnownPosition(true);




/*
	const QUrl url("http://v1ki.com/upload.php");
	QString _DB_FILE("/accounts/1000/shared/downloads/hello1.jpg");

	QString bound;
	QString crlf;
	// QString data;
	QByteArray dataToSend;
	QFile file(_DB_FILE);
	file.open(QIODevice::ReadOnly);
	QByteArray data;
	QUrl params;

	QString userString("hello1.jpg");
	QString param1("name");
	QString param2("image");
	params.addQueryItem(param1, userString );
	params.addQueryItem(param2,file.readAll());
	data.append(params.toString());
	// data.remove(0,1);
	// bound = "---------------------------7d935033608e2";
	//crlf = 0x0d;
	// crlf += 0x0a;
	// data = "--" + bound + crlf + "Content-Disposition: form-data; name=\"name\"; ";
	// data += "name=\"hello.jpg";
	// data += crlf + "Content-Type: application/octet-stream" + crlf + crlf;
	// data += + "Content-Type:image/jpeg" ;
	// dataToSend.insert(0,data);
	// dataToSend.append(file.readAll());
	// dataToSend.append(crlf + "--" + bound + "--" + crlf);
	QNetworkRequest request(url);
	// request.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

	if (AppSettings::isUsingHttps()) {
	request.setUrl(QUrl("http://v1ki.com/upload.php"));

	QSslConfiguration config = request.sslConfiguration();
	config.setPeerVerifyMode(QSslSocket::VerifyNone);
	config.setProtocol(QSsl::TlsV1);
	request.setSslConfiguration(config);
	}

	QNetworkReply* reply = m_networkAccessManager->post(request, data);
	connect(reply, SIGNAL(finished()), this, SLOT(onGetReply()));

	cerr << "tom and jerry\n";
	*/
    time_t time_of_day;
    time_of_day = time(NULL);
    cout << "time:" << ctime(&time_of_day) << endl;
	QUrl params;
//	const QString& picLocation("/accounts/1000/shared/voice/"+sd.imei()+".wav");
	QString param1("latitude");
		QString param2("longitude");
		QString gps_name("gps_data");
		QString srl("imeino");
        QString reply2("reply");
		QString tim("time");
	//QString uploadUrl = "http://v1ki.com/projectx/nuance.php";
		//QString uploadUrl = "http://grison.arvixe.com/~paulrio/v1ki.com/projectx/nuance.php";
	//	QString uploadUrl = "http://v1ki.com/version1.0/projectx/ask.php";
		QString uploadUrl2 = "http://v1ki.com/versionbeta1.1/projectx/ask.php";
//	QFileInfo fileInfo(picLocation);
//	QFile file(picLocation);
//	if (!file.open(QIODevice::ReadWrite)) {
//	qDebug() << "Can not open file:" << picLocation;
//	return;
//	}


	 fstream f3;

	 f3.open("/accounts/1000/shared/misc/gps_data.dat",ios::in);
	 // f2.flush();
	 string dat7;

	 f3 >>  dat7;
	 cout << "gps read from file : " << dat7 << endl;
	  f3.close();

	QString bound="---------------------------723690991551375881941828858";
	QByteArray serv_data(QString("--"+bound+"\r\n").toAscii());
	serv_data += "Content-Disposition: form-data; name=\"name\"\r\n\r\n";
	serv_data += "\r\n";
	serv_data += QString("--" + bound + "\r\n").toAscii();
//	data += "Content-Disposition: form-data; name=\"image\"; filename=\""+file.fileName()+"\"\r\n";
//	data += "Content-Type: audio/"+fileInfo.suffix().toLower()+"\r\n\r\n";
    //data += file.readAll();
	serv_data += "\r\n";
	serv_data += QString("--" + bound + "\r\n").toAscii();
	serv_data += QString("--" + bound + "\r\n").toAscii();
	serv_data += "Content-Disposition: form-data; name=\"message\"\r\n\r\n";

	serv_data += "\r\n";
	serv_data += "\r\n";

    cout << "hardware ids:" << sd.hardwareId().toStdString() << endl;
    cout << "pin ids:" << sd.pin().toStdString() << endl;
    cout << "serial number ids:" << sd.serialNumber().toStdString() << endl;
    cout << "imei ids:" << sd.imei().toStdString() << endl;
    double imein(sd.imei().toDouble());
	double param3(lastPosition.coordinate().latitude());
    double param4(lastPosition.coordinate().longitude());
    QString dat1 = QString::number(param3);
    QString dat2 = QString::number(param4);
    QString gps_dat(dat7.data());
    QString rep(reply2.data());
    cout << "gps_dat : " << gps_dat.toStdString() << endl;

    QString serialid = QString::number(sd.imei().toLongLong());

     QString times = ctime(&time_of_day);

	params.addQueryItem(param1, dat1);
		params.addQueryItem(param2, dat2);
		params.addQueryItem(srl,serialid);
		params.addQueryItem(tim,times);
		params.addQueryItem(rep,resp2);
		if(gps_dat!= "" || gps_dat!=NULL)
		params.addQueryItem(gps_name, gps_dat );
    cout << "Qstring time:" << times.toStdString() << endl;


		cout << "Lattitude ::::: " << dat1.toStdString()  << endl;
		cout << "Longitude ::::: " << dat2.toStdString()  << endl;

		serv_data.append(params.toString());
	QNetworkRequest request2(uploadUrl2);
	request2.setRawHeader(QByteArray("Content-Type"),QString("multipart/form-data; boundary=" + bound).toAscii());
	request2.setRawHeader(QByteArray("Content-Length"), QString::number(serv_data.length()).toAscii());


	QNetworkReply* reply = m_networkAccessManager->post(request2,serv_data);
	connect(reply, SIGNAL(finished()), this, SLOT(onGetReply()));

	cout << "Server Posting Complete! "   << endl;
cout << "imei qstring no:" << serialid.toStdString() << endl;

}

void PostHttp::dir_post(QString postdata) {
	HardwareInfo sd;
   QStringList pdats;
   //pdats = new QString[2];
   pdats= postdata.split(",");

   //postdata.sp

	 DeviceInfo *bbdvce = new DeviceInfo();
  //  cout << "nuance url data from qml : "+ pdats.at(0).toStdString() << endl;
 //   cout << "user settings data from qml : "+ pdats.at(1).toStdString() << endl;

 //cout << "device info:"    << endl;
	cout << "Uploading... "   << endl;
    QGeoPositionInfoSource *src = QGeoPositionInfoSource::createDefaultSource(this);
    src->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
    QGeoPositionInfo lastPosition2 = src->lastKnownPosition(true);
if(src){
	cout << "gps source exist"  <<  endl;
	bool res =connect(src, SIGNAL(positionUpdated(QGeoPositionInfo)), this,
					SLOT(positionUpdated(QGeoPositionInfo)));
	cout << "source connected information : " << res << endl;
	cout << "gps1"  <<  endl;
		//	Q_ASSERT(res);
		//	Q_UNUSED(res);
			cout << "gps2"  <<  endl;
			src->setPreferredPositioningMethods(
			QGeoPositionInfoSource::SatellitePositioningMethods);
			src->setUpdateInterval(1000);
			cout << "gps3"  <<  endl;
	 		src->setProperty("backgroundMode", true);
	                  src->startUpdates();


	        cout << "updated satelite information: " << lastPosition2.coordinate().latitude() << endl;

	        cout << "updated satelite information: " << lastPosition2.coordinate().longitude() << endl;

}
	else
	cout << "gps no source exist" << endl;
cout << "updated satelite information2: " << lastPosition2.coordinate().latitude() << endl;

cout << "updated satelite information2: " << lastPosition2.coordinate().longitude() << endl;

    // Connect the positionUpdated() signal to a
    // slot that handles position updates.
    src->setUpdateInterval(1);
src->startUpdates();

cout << "supported sources:" << src->preferredPositioningMethods() << endl;

    bool positionUpdatedConnected = connect(src,
        SIGNAL(positionUpdated(const QGeoPositionInfo &)),
        this,
        SLOT(positionUpdated(const QGeoPositionInfo &)));


    if (positionUpdatedConnected) {
        // Signal was successfully connected.
        // Request a single fix and wait for the
        // positionUpdated() signal to be emitted.
        src->requestUpdate();
        src->requestUpdate();
        src->requestUpdate();
    	cout << " Successfully  connected to GPS " << endl ;
    } else {
       // Failed to connect to signal.
   	cout << "Failed to connect GPS " << endl ;
   }
 //   src->requestUpdate();
    QGeoPositionInfo lastPosition = src->lastKnownPosition(true);




/*
	const QUrl url("http://v1ki.com/upload.php");
	QString _DB_FILE("/accounts/1000/shared/downloads/hello1.jpg");

	QString bound;
	QString crlf;
	// QString data;
	QByteArray dataToSend;
	QFile file(_DB_FILE);
	file.open(QIODevice::ReadOnly);
	QByteArray data;
	QUrl params;

	QString userString("hello1.jpg");
	QString param1("name");
	QString param2("image");
	params.addQueryItem(param1, userString );
	params.addQueryItem(param2,file.readAll());
	data.append(params.toString());
	// data.remove(0,1);
	// bound = "---------------------------7d935033608e2";
	//crlf = 0x0d;
	// crlf += 0x0a;
	// data = "--" + bound + crlf + "Content-Disposition: form-data; name=\"name\"; ";
	// data += "name=\"hello.jpg";
	// data += crlf + "Content-Type: application/octet-stream" + crlf + crlf;
	// data += + "Content-Type:image/jpeg" ;
	// dataToSend.insert(0,data);
	// dataToSend.append(file.readAll());
	// dataToSend.append(crlf + "--" + bound + "--" + crlf);
	QNetworkRequest request(url);
	// request.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

	if (AppSettings::isUsingHttps()) {
	request.setUrl(QUrl("http://v1ki.com/upload.php"));

	QSslConfiguration config = request.sslConfiguration();
	config.setPeerVerifyMode(QSslSocket::VerifyNone);
	config.setProtocol(QSsl::TlsV1);
	request.setSslConfiguration(config);
	}

	QNetworkReply* reply = m_networkAccessManager->post(request, data);
	connect(reply, SIGNAL(finished()), this, SLOT(onGetReply()));

	cerr << "tom and jerry\n";
	*/
    time_t time_of_day;
    time_of_day = time(NULL);
    cout << "time:" << ctime(&time_of_day) << endl;
	QUrl params;
	//const QString& picLocation("/accounts/1000/shared/voice/"+sd.imei()+".wav");
	const QString& picLocation("data/vloc16k.flac");
	QString param1("latitude");
		QString param2("longitude");
		QString gps_name("gps_data");
		QString srl("imeino");
		QString tim("time");
	//QString uploadUrl = "http://v1ki.com/projectx/nuance.php";
		//QString uploadUrl = "http://grison.arvixe.com/~paulrio/v1ki.com/projectx/nuance.php";
		//QString uploadUrl = "https://dictation.nuancemobility.net/NMDPAsrCmdServlet/dictation?appId=NMDPTRIAL_agrimainfotech20130122095800&appKey=7b0189887ec673aa6a56deb000bcd262876661204c536dc7001a894ac97cf9447e82cdfc4840a6b2968463964035f8c89c5ae8e8ad55afad7039f739b5f17e33&id="+sd.imei();

	//	QString uploadUrl = pdats.at(0);
		QString uploadUrl = "https://www.google.com/speech-api/v1/recognize?xjerr=1&client=chromium&lang=en-US";
	QFileInfo fileInfo(picLocation);
	QFile file(picLocation);
	if (!file.open(QIODevice::ReadWrite)) {
	qDebug() << "Can not open file:" << picLocation;
	return;
	}


	 fstream f3;

	 f3.open("/accounts/1000/shared/misc/gps_data.dat",ios::in);
	 // f2.flush();
	 string dat7;

	 f3 >>  dat7;
	 cout << "gps read from file : " << dat7 << endl;
	  f3.close();

	QString bound="---------------------------723690991551375881941828858";
	QByteArray data(QString("--"+bound+"\r\n").toAscii());
	data += "Content-Disposition: form-data; name=\"name\"\r\n\r\n";
	data += "\r\n";
	data += QString("--" + bound + "\r\n").toAscii();
	data += "Content-Disposition: form-data; name=\"audioFile\"; filename=\""+file.fileName()+"\"\r\n";
	data += "Content-Type: audio/"+fileInfo.suffix().toLower()+"\r\n\r\n";
	data += file.readAll();
	data += "\r\n";
	data += QString("--" + bound + "\r\n").toAscii();
	data += QString("--" + bound + "\r\n").toAscii();
	data += "Content-Disposition: form-data; name=\"message\"\r\n\r\n";

	data += "\r\n";
	data += "\r\n";


    double imein(sd.imei().toDouble());
	double param3(lastPosition.coordinate().latitude());
    double param4(lastPosition.coordinate().longitude());
    QString dat1 = QString::number(param3);
    QString dat2 = QString::number(param4);
    QString gps_dat(dat7.data());
    cout << "gps_dat : " << gps_dat.toStdString() << endl;

    QString serialid = QString::number(sd.imei().toLongLong());

     QString times = ctime(&time_of_day);

	params.addQueryItem(param1, dat1);
		params.addQueryItem(param2, dat2);
		params.addQueryItem(srl,serialid);
		params.addQueryItem(tim,times);
		if(gps_dat!= "" || gps_dat!=NULL)
		params.addQueryItem(gps_name, gps_dat );
    cout << "Qstring time:" << times.toStdString() << endl;


		cout << "Lattitude ::::: " << dat1.toStdString()  << endl;
		cout << "Longitude ::::: " << dat2.toStdString()  << endl;

		data.append(params.toString());
	QNetworkRequest request(uploadUrl);
	//request.setRawHeader(QByteArray("Content-Type"),QString("multipart/form-data; boundary=" + bound).toAscii());
	request.setRawHeader(QByteArray("Content-Type"),QString("audio/x-flac; rate=16000").toAscii());
//	request.setRawHeader(QByteArray("Content-Length"), QString::number(data.length()).toAscii());
  //  request.setRawHeader(QByteArray("Content-Language"), QString("en_US").toAscii());
 //   request.setRawHeader(QByteArray("Accept-Language"), QString("en_US").toAscii());
	//   request.setRawHeader(QByteArray("Content-Language"), QString( pdats.at(1)).toAscii());
 //   request.setRawHeader(QByteArray("Accept-Language"), QString( pdats.at(1)).toAscii());
  //  request.setRawHeader(QByteArray("Accept"), QString("text/plain").toAscii());
 //      request.setRawHeader(QByteArray("Accept-Topic"), QString("Dictation").toAscii());


	QNetworkReply* reply = m_networkAccessManager->post(request,data);
	connect(reply, SIGNAL(finished()), this, SLOT(onGetReply()));

	cout << "Direct Posting Complete! "   << endl;
cout << "imei qstring no:" << serialid.toStdString() << endl;

}

void PostHttp::wav_resample() {

	HardwareInfo sd2;
		cerr << "sampling...";
		WavHeader h, he; // h - header as is, he - adjusted endianness

		const char* path1;
		const char* path2;
		const char* path3;

		path1 = "/accounts/1000/shared/misc/vloc.wav";
		//path2 = ("/accounts/1000/shared/voice/"+sd2.imei().toStdString()+".wav").c_str();
		path2 =  "/accounts/1000/shared/voice/vloc16k.wav";
		path3 = ("data/vloc16k.flac");
		cerr << "file:" << path1 << "out_file: "<<path2;
		ifstream ifile;
		ofstream ofile;
		ifile.open(path1, ios::binary);
		ofile.open(path2, ios::binary);
		if (!ifile) {
			cerr << "can't open input file\n";
		}
		if (!ofile) {
			cerr << "can't open output file\n";
		}

		ifile.seekg(0, ios::end);
		int length = ifile.tellg();
		ifile.seekg(0, ios::beg);

		if (length < 44) {
			cerr << "Input file is corrupt.\n";

		}

		ifile.read((char *) (&h), sizeof(h));
		ifile.seekg(0, ios::beg);
		ifile.read((char *) (&he), sizeof(he));
		if (is_big_endian()) {
			he.file_size_8 = swap_endian<int32_t>(he.file_size_8);
			he.fmt_size_8 = swap_endian<int32_t>(he.fmt_size_8);
			he.audio_format = swap_endian<int16_t>(he.audio_format);
			he.num_channels = swap_endian<int16_t>(he.num_channels);
			he.sample_rate = swap_endian<int32_t>(he.sample_rate);
			he.byte_rate = swap_endian<int32_t>(he.byte_rate);
			he.block_align = swap_endian<int16_t>(he.block_align);
			he.bps = swap_endian<int16_t>(he.bps);

		}

		cout << "Size of input file: " << length << endl;
		cout << "Size from header: " << (he.file_size_8 + 8) << endl;

		cout << "File type format (expecting RIFF): " << h.riff[0] << h.riff[1]
				<< h.riff[2] << h.riff[3] << endl;
		if (h.riff[0] != 'R' || h.riff[1] != 'I' || h.riff[2] != 'F'
				|| h.riff[3] != 'F') {
			cerr << "Wrong file type\n";

		}
		cout << "Audio format (expecting 1 for PCM): " << he.audio_format << endl;
		if (he.audio_format != 1) {
			cerr << "Wrong audio format\n";

		}
		cout << "Number of channels (expecting 2 for stereo): " << he.num_channels
				<< endl;
		if (he.num_channels != 2) {
			cerr << "Wrong number of channels\n";

		}
		cout << "Sample rate (expecting 48000): " << he.sample_rate << endl;
		if (he.sample_rate != 48000) {
			cerr << "Wrong sample rate\n";

		}
		cout << "Bits per Sample (expecting 16): " << he.bps << endl;
		if (he.bps != 16) {
			cerr << "Wrong bits per sample\n";

		}

		// use header h for output
		h.num_channels = 1;
		h.sample_rate = 16000;
		h.byte_rate = h.sample_rate * h.num_channels * (he.bps / 8);
		h.block_align = h.num_channels * (he.bps / 8);
		if (is_big_endian()) {
			h.num_channels = swap_endian<int16_t>(h.num_channels);
			h.sample_rate = swap_endian<int32_t>(h.sample_rate);
			h.byte_rate = swap_endian<int16_t>(h.byte_rate);
			h.block_align = swap_endian<int16_t>(h.block_align);
		}
		// output the modified header
		ofile.write((char *) (&h), sizeof(h));

		// skip to the data header
		char needed_chunk[] = "data";
		char chunk_name[5];
		int32_t chunk_size;
		int32_t output_chunk_size; // chunk_size / 6

		while (1) {
			ifile.read(chunk_name, 4);
			if (ifile.eof() || ifile.fail() || ifile.bad()) {
				break;
			}
			chunk_name[4] = '\0';
			ifile.read((char *) (&chunk_size), sizeof(chunk_size));
			if (is_big_endian()) {
				chunk_size = swap_endian<int32_t>(chunk_size);
			}
			if (strcmp(chunk_name, needed_chunk) == 0) {
				break;
			}
		}

		cout << "Size of data in bytes: " << chunk_size << endl;
		output_chunk_size = chunk_size / 3;
		cout << "chunk " << chunk_size;
		if (is_big_endian()) {
			swap_endian<int32_t>(output_chunk_size);
		}
		ofile.write(chunk_name, 4);
		ofile.write((char *) (&output_chunk_size), sizeof(output_chunk_size));
		// now, ifile is pointing at the beginning of data

		resample(ifile, ofile, 48000, 16000, chunk_size);

		ifile.close();
		ofile.close();
		cout << "Resampling Complete! "   << endl;
	//	const char *url("calendar://");
		//	char **err;
		//	err[0]="hello1";
		//	err[1] = "hello2";
		//	  navigator_invoke(url,err );

	//			cout << "Invoking Complete! "   << endl;


	    OurEncoder::ConvertWaveFile(path2,path3);
}

/**
 * PostHttp::onGetReply()
 *
 * SLOT
 * Read and return the http response from our http post request
 */

void PostHttp::gps_update(QString gps_data ) {
 cout << "gps function data:" << gps_data.toStdString() << endl;
 fstream f2;
 f2.open("/accounts/1000/shared/misc/gps_data.dat",ios::out);
 // f2.flush();
 if(!(gps_data == NULL || gps_data == ""))
  f2 << gps_data.toStdString();
  f2.close();

}

void PostHttp::onGetReply() {


	QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

	QString response;
	if (reply) {
		if (reply->error() == QNetworkReply::NoError) {
			const int available = reply->bytesAvailable();
			if (available > 0) {
				const QByteArray buffer(reply->readAll());
				response = QString::fromUtf8(buffer);
			}
		} else {
			response =
					tr("Error: %1 status: %2").arg(reply->errorString(),
							reply->attribute(
									QNetworkRequest::HttpStatusCodeAttribute).toString());


		}

		reply->deleteLater();
	}

	if (response.trimmed().isEmpty()) {
		response = tr("Network error! Please try again!");
	}

	cout << "Response" << response.toStdString() << endl;
	string filtered_response,filtered_response2;
 	if(response.contains("utterance")){
 	  filtered_response = getBetween(response.toStdString(),"utterance","confidence");
 	   filtered_response2 = getBetween(filtered_response,"\":\"","\",");
         response = QString(filtered_response2.data());
       cout << "modified response :" << response.toStdString() << endl;
    gdata = response;
    resp2 = response;
    cout << "response to int :" << response.toInt() << endl;
    int rrt = response.toInt();
    if(response.toInt()==0)
    	response = "###";

 	}
 	else
 		response = "###";


//	 cout << "filtered Response : " << filtered_response2 << endl;
//	 response = filtered_response2.c_str();
	emit complete(response);

cout << "emiting response :" << response.toStdString() << endl;

//ofstream htmlfile;

//	htmlfile.open("local:///assets/viki_html.html");
//htmlfile.clear();
//htmlfile<<"<html><head></head><body><h2>viki</h2>" <<endl;
//htmlfile<<"<h2>thid is viki</h2></body></html>";
//fstream f,in;
//f.open("/accounts/1000/shared/downloads/viki_html.html",ios::out);
//f.open("/accounts/1000/shared/misc/viki_html.html",ios::out);
//in.open("asset:///header.txt",ios::in);
 //f<<"<html><head></head><body><h2>viki</h2></body></html>"<<endl;
// f.flush();
 //string dat,line;
// f<< "<!DOCTYPE HTML><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" /><script src=\"local:///chrome/webworks.js\" type=\"text/javascript\"></script<script type=\"text/javascript\" src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.7.2/jquery.min.js\"></script><link rel=\"stylesheet\" href=\"http://www.v1ki.com/css/index.css\" /></head><body><div id=\"my\">" << endl;
// f<<'<html><head><meta name="viewport" content="width=device-width, initial-scale=1" /><script src="local:///chrome/webworks.js" type="text/javascript"></script><script type="text/javascript" src="https://ajax.googleapis.com/ajax/libs/jquery/1.7.2/jquery.min.js"></script><script type="text/javascript">$(document).ready(function(){document.getElementById( "bottom" ).scrollIntoView();});</script><link rel="stylesheet" href="local:///assets/index.css" /></head><body><div id="my">'<< endl;
//if(in.is_open()){
	//while(in.good()){
	//	getline(in,dat);
	//	in>>line;
	// 	cout<<"line data:"<<line<<endl;
	//	cout<<"dat data:"<<dat<<endl;
// f<<line;
	//}
 //f<< response.toStdString()<<endl;
	//in.close();
//}
 //f << "<html><head></head><body>" << endl;

	  	 string replaced_data = ReplaceString(response.toStdString(), "\"", "\\\"");
	//	 QString repl_data = ReplaceString(response, "\"", "\\\"");
	  string btdata = getBetween(replaced_data,",","<");
	  string::size_type p = btdata.rfind(',');


		if(!response.contains("Error")){

	     	if(tmp1.isNull())
	     	  tmp1 =response;
	     	else if(tmp2.isNull())
	     		tmp2  = response;
	     	else if(tmp3.isNull())
	     		tmp3 = response;
	     	else if(!tmp3.isNull())
	     	{
	     		tmp1=tmp2;
	     		tmp2=tmp3;
	     		tmp3 = response;
	     	}
	     QStringList  rest;

		}

cout<<response.toStdString()<<endl;

     if(!(btdata.empty() || btdata ==  "")){

	  cout << "filtered data :" << btdata << endl;

}



}
string getBetween(string fullstring, string start, string end)
{
int spos = fullstring.find(start) + start.length();
int epos = fullstring.find(end);

int length = (epos - spos);
cout << "returing string : " << fullstring.substr(spos, length) << endl;
return fullstring.substr(spos, length);
}


std::string ReplaceString(std::string subject, const std::string& search,
                         const std::string& replace) {
   size_t pos = 0;
   while((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
   }
   return subject;
}







//! [1]
