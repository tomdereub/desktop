/*
 * Copyright (C) by Oleksandr Zolotov <alex@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QLocalServer>
#include <QMutex>
#include <QSize>
#include <QVariant>

class QLocalSocket;

namespace OCC {
class ShellExtensionsServer : public QObject
{
    struct ThumbnailRequestInfo
    {
        QString path;
        QSize size;
        QString folderAlias;

        bool isValid() const { return !path.isEmpty() && !size.isEmpty() && !folderAlias.isEmpty(); }
    };

    struct CustomStateRequestInfo
    {
        QString path;
        QString folderAlias;

        bool isValid() const { return !path.isEmpty() && !folderAlias.isEmpty(); }
    };

    Q_OBJECT
public:
    ShellExtensionsServer(QObject *parent = nullptr);
    ~ShellExtensionsServer() override;

    void setShareStateInvalidationInterval(qint64 interval);

signals:
    void fetchSharesJobFinished(const QString &folderAlias);

private:
    void sendJsonMessageWithVersion(QLocalSocket *socket, const QVariantMap &message);
    void sendEmptyDataAndCloseSession(QLocalSocket *socket);
    void closeSession(QLocalSocket *socket);
    void processCustomStateRequest(QLocalSocket *socket, const CustomStateRequestInfo &customStateRequestInfo);
    void processThumbnailRequest(QLocalSocket *socket, const ThumbnailRequestInfo &thumbnailRequestInfo);

    void parseCustomStateRequest(QLocalSocket *socket, const QVariantMap &message);
    void parseThumbnailRequest(QLocalSocket *socket, const QVariantMap &message);

private slots:
    void slotNewConnection();
    void slotSharesFetched(const QJsonDocument &reply);
    void slotSharesFetchError(int statusCode, const QString &message);

private:
    QLocalServer _localServer;
    QMutex _runningFetchShareJobsMutex;
    QStringList _runningFetchShareJobsForPaths;
    QMutex _customStateSocketConnectionsMutex;
    QMap<qintptr, QMetaObject::Connection> _customStateSocketConnections;
    qint64 _shareStateInvalidationInterval = 0;
};
} // namespace OCC
