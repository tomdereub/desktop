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

#include "shellextensionsserver.h"
#include "account.h"
#include "accountstate.h"
#include "common/shellextensionutils.h"
#include "folder.h"
#include "folderman.h"
#include "ocssharejob.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>

namespace OCC {

ShellExtensionsServer::ShellExtensionsServer(QObject *parent)
    : QObject(parent)
{
    _localServer.listen(VfsShellExtensions::serverNameForApplicationNameDefault());
    connect(&_localServer, &QLocalServer::newConnection, this, &ShellExtensionsServer::slotNewConnection);
}

ShellExtensionsServer::~ShellExtensionsServer()
{
    for (const auto &connection : _customStateSocketConnection) {
        if (connection) {
            QObject::disconnect(connection);
        }
    }
    _customStateSocketConnection.clear();

    if (!_localServer.isListening()) {
        return;
    }
    _localServer.close();
}

void ShellExtensionsServer::sendJsonMessageWithVersion(QLocalSocket *socket, const QVariantMap &message)
{
    socket->write(VfsShellExtensions::Protocol::createJsonMessage(message));
    socket->waitForBytesWritten();
}

void ShellExtensionsServer::sendEmptyDataAndCloseSession(QLocalSocket *socket)
{
    sendJsonMessageWithVersion(socket, QVariantMap{});
    closeSession(socket);
}

void ShellExtensionsServer::closeSession(QLocalSocket *socket)
{
    connect(socket, &QLocalSocket::disconnected, this, [socket] {
        socket->close();
        socket->deleteLater();
    });
    socket->disconnectFromServer();
}

void ShellExtensionsServer::processCustomStateRequest(
    QLocalSocket *socket, const CustomStateRequestInfo &customStateRequestInfo)
{
    if (!customStateRequestInfo.isValid()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto folder = FolderMan::instance()->folder(customStateRequestInfo.folderAlias);

    if (!folder) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto fileInfo = QFileInfo(customStateRequestInfo.path);
    const auto filePathRelative = QFileInfo(customStateRequestInfo.path).canonicalFilePath().remove(folder->path());

    SyncJournalFileRecord record;
    if (!folder->journalDb()->getFileRecord(filePathRelative, &record) || !record.isValid()
        || record.path().isEmpty()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    constexpr auto sharesFetchInterval = 30 * 1000;

    if (QDateTime::currentMSecsSinceEpoch() - record._lastShareStateFetchedTimestmap < sharesFetchInterval) {
        const auto messageReplyWithCustomStates = QVariantMap{{VfsShellExtensions::Protocol::CustomStateDataKey,
            QVariantMap{
                { QLatin1Literal("isLocked"), record._lockstate._locked },
                { QLatin1Literal("isShared"), record._isShared} }
            }
        };
        qInfo() << record.path() << " record._lastShareStateFetchedTimestmap has less than " << sharesFetchInterval << " ms difference with QDateTime::currentMSecsSinceEpoch(). Returning data from SyncJournal.";
        sendJsonMessageWithVersion(socket, messageReplyWithCustomStates);
        closeSession(socket);
        return;
    }

    auto recordPathSplit = record.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString path = QStringLiteral("/");

    if (recordPathSplit.size() > 1) {
        recordPathSplit.removeLast();
        path = recordPathSplit.join(QLatin1Char('/'));
    }

    auto *job = new OcsShareJob(folder->accountState()->account());
    job->setProperty("folderAlias", customStateRequestInfo.folderAlias);
    connect(job, &OcsShareJob::shareJobFinished, this, &ShellExtensionsServer::slotSharesFetched);
    connect(job, &OcsJob::ocsError, this, &ShellExtensionsServer::slotSharesFetchError);

    _customStateSocketConnection.insert(socket->socketDescriptor(),
        QObject::connect(this, &ShellExtensionsServer::fetchSharesJobFinished,
            [this, socket, customStateRequestInfo, filePathRelative](const QString &path) {
                const auto connection = _customStateSocketConnection[socket->socketDescriptor()];
                if (connection) {
                    QObject::disconnect(connection);
                }
                _customStateSocketConnection.remove(socket->socketDescriptor());

                const auto folder = FolderMan::instance()->folder(customStateRequestInfo.folderAlias);
                SyncJournalFileRecord record;
                if (!folder || !folder->journalDb()->getFileRecord(filePathRelative, &record) || !record.isValid()) {
                    sendEmptyDataAndCloseSession(socket);
                    return;
                }
                const auto messageReplyWithCustomStates = QVariantMap{{VfsShellExtensions::Protocol::CustomStateDataKey,
                    QVariantMap {
                        { QLatin1Literal("isLocked"), record._lockstate._locked },
                        { QLatin1Literal("isShared"), record._isShared} }
                    }
                };
                qInfo() << "Sending reply from OcsShareJob for socket: " << socket->socketDescriptor() << " and record: " << record.path();
                sendJsonMessageWithVersion(socket, messageReplyWithCustomStates);
                closeSession(socket);
            }));

    QMutexLocker locker(&_runningFetchShareJobsMutex);
    if (!_runningFetchShareJobsForPaths.contains(path)) {
        _runningFetchShareJobsForPaths.push_back(path);
        qInfo() << "Started OcsShareJob for path: " << path;
        job->getShares(path, {{QStringLiteral("subfiles"), QStringLiteral("true")}});
    } else {
        qInfo() << "OcsShareJob is already running for path: " << path;
    }
}

void ShellExtensionsServer::processThumbnailRequest(QLocalSocket *socket, const ThumbnailRequestInfo &thumbnailRequestInfo)
{
    if (!thumbnailRequestInfo.isValid()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto folder = FolderMan::instance()->folder(thumbnailRequestInfo.folderAlias);

    if (!folder) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto fileInfo = QFileInfo(thumbnailRequestInfo.path);
    const auto filePathRelative = QFileInfo(thumbnailRequestInfo.path).canonicalFilePath().remove(folder->path());

    SyncJournalFileRecord record;
    if (!folder->journalDb()->getFileRecord(filePathRelative, &record) || !record.isValid()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    QUrlQuery queryItems;
    queryItems.addQueryItem(QStringLiteral("fileId"), record._fileId);
    queryItems.addQueryItem(QStringLiteral("x"), QString::number(thumbnailRequestInfo.size.width()));
    queryItems.addQueryItem(QStringLiteral("y"), QString::number(thumbnailRequestInfo.size.height()));
    const QUrl jobUrl = Utility::concatUrlPath(folder->accountState()->account()->url(), QStringLiteral("/index.php/core/preview"), queryItems);
    const auto job = new SimpleNetworkJob(folder->accountState()->account());
    job->startRequest(QByteArrayLiteral("GET"), jobUrl);
    connect(job, &SimpleNetworkJob::finishedSignal, this, [socket, this](QNetworkReply *reply) {
        const auto contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        if (!contentType.startsWith(QByteArrayLiteral("image/"))) {
            sendEmptyDataAndCloseSession(socket);
            return;
        }
        
        auto messageReplyWithThumbnail = QVariantMap {
            {VfsShellExtensions::Protocol::ThumnailProviderDataKey, reply->readAll().toBase64()}
        };
        sendJsonMessageWithVersion(socket, messageReplyWithThumbnail);
        closeSession(socket);
    });
}

void ShellExtensionsServer::slotNewConnection()
{
    const auto socket = _localServer.nextPendingConnection();

    if (!socket) {
        return;
    }

    socket->waitForReadyRead();
    const auto message = QJsonDocument::fromJson(socket->readAll()).toVariant().toMap();

    if (!VfsShellExtensions::Protocol::validateProtocolVersion(message)) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    if (message.contains(VfsShellExtensions::Protocol::ThumbnailProviderRequestKey)) {
        parseThumbnailRequest(socket, message);
        return;
    } else if (message.contains(VfsShellExtensions::Protocol::CustomStateProviderRequestKey)) {
        parseCustomStateRequest(socket, message);
        return;
    }

    sendEmptyDataAndCloseSession(socket);
    return;
}

void ShellExtensionsServer::slotSharesFetched(const QJsonDocument &reply)
{
    const auto job = qobject_cast<OcsShareJob *>(sender());

    Q_ASSERT(job);
    if (!job) {
        qWarning() << "ShellExtensionsServer::slotSharesFetched is not called by OcsShareJob's signal!";
        return;
    }

    const auto sharesPath = job->getParamValue(QStringLiteral("path"));
    
    QMutexLocker locker(&_runningFetchShareJobsMutex);
    _runningFetchShareJobsForPaths.removeAll(sharesPath);

    const auto folderAlias = job->property("folderAlias").toString();

    Q_ASSERT(!folderAlias.isEmpty());
    if (folderAlias.isEmpty()) {
        qWarning() << "No 'folderAlias' set for OcsShareJob's instance!";
        return;
    }

    const auto folder = FolderMan::instance()->folder(folderAlias);

    Q_ASSERT(folder);
    if (!folder) {
        qWarning() << "folder not found for folderAlias: " << folderAlias;
        return;
    }

    QStringList sharesToReset;
    const QString shareesToResetPath = sharesPath == QStringLiteral("/") ? QStringLiteral("") : sharesPath;
    folder->journalDb()->listFilesInPath(shareesToResetPath.toUtf8(), [&](const SyncJournalFileRecord &rec) { sharesToReset.push_back(rec.path()); });

    const auto timeStamp = QDateTime::currentMSecsSinceEpoch();

    for (const auto &shareToResetPat : sharesToReset) {
        SyncJournalFileRecord record;
        if (!folder->journalDb()->getFileRecord(shareToResetPat, &record) || !record.isValid()) {
            continue;
        }
        record._isShared = false;
        record._lastShareStateFetchedTimestmap = timeStamp;
        folder->journalDb()->setFileRecord(record);
    }

    const auto sharesFetched = reply.object().value("ocs").toObject().value("data").toArray();

    for (const auto &share : sharesFetched) {
        const auto shareData = share.toObject();
        const auto sharePath = [&shareData]() { 
            auto pathTemp = shareData.value("path").toString();
            if (pathTemp.size() > 1 && pathTemp.startsWith(QLatin1Char('/'))) {
                pathTemp.remove(0, 1);
            }
            return pathTemp; 
        }();

        SyncJournalFileRecord record;
        if (!folder || !folder->journalDb()->getFileRecord(sharePath, &record) || !record.isValid()) {
            continue;
        }
        record._isShared = true;
        record._lastShareStateFetchedTimestmap = timeStamp;
        folder->journalDb()->setFileRecord(record);
    }

    qInfo() << "Succeeded OcsShareJob for path: " << sharesPath;
    emit fetchSharesJobFinished(sharesPath);
}

void ShellExtensionsServer::slotSharesFetchError(int statusCode, const QString &message)
{
    const auto job = qobject_cast<OcsShareJob *>(sender());

    Q_ASSERT(job);
    if (!job) {
        qWarning() << "ShellExtensionsServer::slotSharesFetched is not called by OcsShareJob's signal!";
        return;
    }

    const auto sharesPath = job->getParamValue(QStringLiteral("path"));

    QMutexLocker locker(&_runningFetchShareJobsMutex);
    _runningFetchShareJobsForPaths.removeAll(sharesPath);

    emit fetchSharesJobFinished(sharesPath);
    qWarning() << "Failed OcsShareJob for path: " << sharesPath;
}

void ShellExtensionsServer::parseCustomStateRequest(QLocalSocket *socket, const QVariantMap &message)
{
    const auto customStateRequestMessage = message.value(VfsShellExtensions::Protocol::CustomStateProviderRequestKey).toMap();
    const auto itemFilePath = QDir::fromNativeSeparators(customStateRequestMessage.value(VfsShellExtensions::Protocol::FilePathKey).toString());

    if (itemFilePath.isEmpty()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    QString foundFolderAlias;
    for (const auto folder : FolderMan::instance()->map()) {
        if (itemFilePath.startsWith(folder->path())) {
            foundFolderAlias = folder->alias();
            break;
        }
    }

    if (foundFolderAlias.isEmpty()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }
    
    const auto customStateRequestInfo = CustomStateRequestInfo {
        itemFilePath,
        foundFolderAlias
    };

    processCustomStateRequest(socket, customStateRequestInfo);
}

void ShellExtensionsServer::parseThumbnailRequest(QLocalSocket *socket, const QVariantMap &message)
{
    const auto thumbnailRequestMessage = message.value(VfsShellExtensions::Protocol::ThumbnailProviderRequestKey).toMap();
    const auto thumbnailFilePath = QDir::fromNativeSeparators(thumbnailRequestMessage.value(VfsShellExtensions::Protocol::FilePathKey).toString());
    const auto thumbnailFileSize = thumbnailRequestMessage.value(VfsShellExtensions::Protocol::ThumbnailProviderRequestFileSizeKey).toMap();

    if (thumbnailFilePath.isEmpty() || thumbnailFileSize.isEmpty()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    QString foundFolderAlias;
    for (const auto folder : FolderMan::instance()->map()) {
        if (thumbnailFilePath.startsWith(folder->path())) {
            foundFolderAlias = folder->alias();
            break;
        }
    }

    if (foundFolderAlias.isEmpty()) {
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto thumbnailRequestInfo = ThumbnailRequestInfo {
        thumbnailFilePath,
        QSize(thumbnailFileSize.value("width").toInt(), thumbnailFileSize.value("height").toInt()),
        foundFolderAlias
    };

    processThumbnailRequest(socket, thumbnailRequestInfo);
}

} // namespace OCC
