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

#ifndef POSTHTTP_HPP
#define POSTHTTP_HPP

#include <QtCore/QObject>
#include <cstring>
#include <string>
class QNetworkAccessManager;

class PostHttp: public QObject {
Q_OBJECT

public:
	PostHttp(QObject* parent = 0);

public Q_SLOTS:
	Q_INVOKABLE	void post();
	Q_INVOKABLE	void dir_post(QString postdata);
	Q_INVOKABLE	void wav_resample();
	Q_INVOKABLE	void gps_update(QString gps_data);

Q_SIGNALS:
	void complete(const QString &info);

private Q_SLOTS:
	void onGetReply();

private:
	QNetworkAccessManager* m_networkAccessManager;
};

#endif
