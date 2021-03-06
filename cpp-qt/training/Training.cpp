/***************
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
***************/
#include "Training.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QtDebug>


Training::Training(QObject *parent) : QObject(parent) {
    connect(&client, &CortexClient::connected, this, &Training::onConnected);
    connect(&client, &CortexClient::disconnected, this, &Training::onDisconnected);
    connect(&client, &CortexClient::errorReceived, this, &Training::onErrorReceived);
    connect(&client, &CortexClient::getDetectionInfoOk, this, &Training::onGetDetectionInfoOk);
    connect(&client, &CortexClient::subscribeOk, this, &Training::onSubscribeOk);
    connect(&client, &CortexClient::trainingOk, this, &Training::onTrainingOk);
    connect(&client, &CortexClient::streamDataReceived, this, &Training::onStreamDataReceived);
    connect(&finder, &HeadsetFinder::headsetsFound, this, &Training::onHeadsetsFound);
    connect(&creator, &SessionCreator::sessionCreated, this, &Training::onSessionCreated);
}

void Training::start(QString detection) {
    this->detection = detection;
    actionIndex = 0;
    trainingFailure = 0;
    client.open();
}

void Training::onConnected() {
    qInfo() << "Connected to Cortex.";
    client.getDetectionInfo(detection);
}

void Training::onDisconnected() {
    qInfo() << "Disconnected.";
    QCoreApplication::quit();
}

void Training::onErrorReceived(QString method, int code, QString error) {
    qCritical() << "Cortex returned an error:";
    qCritical() << "\t" << method << code << error;
    QCoreApplication::quit();
}

void Training::onGetDetectionInfoOk(QStringList actions,
                                    QStringList controls,
                                    QStringList events) {
    this->actions = actions;
    qInfo() << "Information for" << detection << ":";
    qInfo() << "Actions " << actions;
    qInfo() << "Controls" << controls;
    qInfo() << "Events  " << events;
    finder.findHeadsets(&client);
}

void Training::onHeadsetsFound(const QList<Headset> &headsets) {
    headsetId = headsets.first().id;
    finder.clear();
    creator.createSession(&client, headsetId, "");
}

void Training::onSessionCreated(QString token, QString sessionId) {
    this->token = token;
    this->sessionId = sessionId;
    creator.clear();
    client.subscribe(token, sessionId, "sys");
}

void Training::onSubscribeOk(QString sid) {
    qInfo() << "Subscription to sys stream successful, sid" << sid;
    client.training(token, sessionId, detection, action(), "start");
}

void Training::onTrainingOk(QString msg) {
    Q_UNUSED(msg);
    // this signal is not important
    // instead we need to watch the events from the sys stream
}

void Training::onStreamDataReceived(QString sessionId, QString stream,
                                    double time, const QJsonArray &data) {
    Q_UNUSED(sessionId);
    Q_UNUSED(stream);
    Q_UNUSED(time);
    //qDebug() << " * sys data:" << data;

    if (isEvent(data, "Started")) {
        qInfo() << "";
        qInfo() << "Please, focus on the action" << action().toUpper()
                << "for a few seconds.";
    }
    else if (isEvent(data, "Succeeded")) {
        // the training of this action is a success
        // we "accept" it, and then we will receive the "Completed" event
        client.training(token, sessionId, detection, action(), "accept");
    }
    else if (isEvent(data, "Failed")) {
        retryAction();
    }
    else if (isEvent(data, "Completed")) {
        qInfo() << "Well done! You successfully trained " << action();
        nextAction();
    }
}

void Training::nextAction() {
    actionIndex++;
    trainingFailure = 0;

    if (actionIndex < 3 && actionIndex < actions.size()) {
        // ok, let's train the next action
        client.training(token, sessionId, detection, action(), "start");
    }
    else {
        // that's enough training for today
        qInfo() << "Done.";
        QCoreApplication::quit();
    }
}

void Training::retryAction() {
    trainingFailure++;

    if (trainingFailure < 3) {
        qInfo() << "Sorry, it didn't work. Let's try again.";
        client.training(token, sessionId, detection, action(), "start");
    }
    else {
        qInfo() << "It seems you are struggling with this action. Let's try another one.";
        nextAction();
    }
}

bool Training::isEvent(const QJsonArray &data, QString event) {
    for (const QJsonValue &val : data) {
        QString str = val.toString();
        if (str.endsWith(event)) {
            return true;
        }
    }
    return false;
}
