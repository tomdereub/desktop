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
#include "libsync/vfs/cfapi/shellext/configvfscfapishellext.h"
#include "folder.h"
#include "folderman.h"
#include "ocssharejob.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>

namespace {
constexpr auto customStatesSharesFetchInterval = 2 * 60 * 1000; // 2 minutes, so we don't make fetch sharees requests too often
constexpr auto folderAliasPropertyKey = "folderAlias";
}

namespace OCC {

ShellExtensionsServer::ShellExtensionsServer(QObject *parent)
    : QObject(parent)
{
    _shareStateInvalidationInterval = customStatesSharesFetchInterval;
    _localServer.listen(VfsShellExtensions::serverNameForApplicationNameDefault());
    connect(&_localServer, &QLocalServer::newConnection, this, &ShellExtensionsServer::slotNewConnection);
}

ShellExtensionsServer::~ShellExtensionsServer()
{
    {
        QMutexLocker locker(&_customStateSocketConnectionsMutex);
        for (const auto &connection : _customStateSocketConnections) {
            if (connection) {
                QObject::disconnect(connection);
            }
        }
        _customStateSocketConnections.clear();
    }

    if (!_localServer.isListening()) {
        return;
    }
    _localServer.close();
}

QString ShellExtensionsServer::getFetchThumbnailPath()
{
    return QStringLiteral("/index.php/core/preview");
}

void ShellExtensionsServer::setShareStateInvalidationInterval(qint64 interval)
{
    _shareStateInvalidationInterval = interval;
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

void ShellExtensionsServer::processCustomStateRequest(QLocalSocket *socket, const CustomStateRequestInfo &customStateRequestInfo)
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
    const auto filePathRelative = QString(customStateRequestInfo.path).remove(folder->path());

    SyncJournalFileRecord record;
    if (!folder->journalDb()->getFileRecord(filePathRelative, &record) || !record.isValid() || record.path().isEmpty()) {
        qWarning() << "Record not found in SyncJournal for: " << filePathRelative;
        sendEmptyDataAndCloseSession(socket);
        return;
    }

    const auto composeMessageReplyFromRecord = [](const SyncJournalFileRecord &record) {
        QVariantList states;
        if (record._lockstate._locked) {
            states.push_back(QString(CUSTOM_STATE_ICON_LOCKED_INDEX).toInt() - QString(CUSTOM_STATE_ICON_INDEX_OFFSET).toInt());
        }
        if (record._isShared) {
            states.push_back(QString(CUSTOM_STATE_ICON_SHARED_INDEX).toInt() - QString(CUSTOM_STATE_ICON_INDEX_OFFSET).toInt());
        }
        return QVariantMap{{VfsShellExtensions::Protocol::CustomStateDataKey,
            QVariantMap{{VfsShellExtensions::Protocol::CustomStateStatesKey, states}}}};
    };

    if (QDateTime::currentMSecsSinceEpoch() - record._lastShareStateFetchedTimestmap < _shareStateInvalidationInterval) {
        qInfo() << record.path() << " record._lastShareStateFetchedTimestmap has less than " << _shareStateInvalidationInterval << " ms difference with QDateTime::currentMSecsSinceEpoch(). Returning data from SyncJournal.";
        sendJsonMessageWithVersion(socket, composeMessageReplyFromRecord(record));
        closeSession(socket);
        return;
    }

    const auto job = new OcsShareJob(folder->accountState()->account());
    job->setProperty(folderAliasPropertyKey, customStateRequestInfo.folderAlias);
    connect(job, &OcsShareJob::shareJobFinished, this, &ShellExtensionsServer::slotSharesFetched);
    connect(job, &OcsJob::ocsError, this, &ShellExtensionsServer::slotSharesFetchError);

    {
        QMutexLocker locker(&_customStateSocketConnectionsMutex);
        _customStateSocketConnections.insert(socket->socketDescriptor(), QObject::connect(this, &ShellExtensionsServer::fetchSharesJobFinished, [this, socket, filePathRelative, composeMessageReplyFromRecord](const QString &folderAlias) {
            {
                QMutexLocker locker(&_customStateSocketConnectionsMutex);
                const auto connection = _customStateSocketConnections[socket->socketDescriptor()];
                if (connection) {
                    QObject::disconnect(connection);
                }
                _customStateSocketConnections.remove(socket->socketDescriptor());
            }
            
            const auto folder = FolderMan::instance()->folder(folderAlias);
            SyncJournalFileRecord record;
            if (!folder || !folder->journalDb()->getFileRecord(filePathRelative, &record) || !record.isValid()) {
                qWarning() << "Record not found in SyncJournal for: " << filePathRelative;
                sendEmptyDataAndCloseSession(socket);
                return;
            }
            
            qInfo() << "Sending reply from OcsShareJob for socket: " << socket->socketDescriptor() << " and record: " << record.path();
            sendJsonMessageWithVersion(socket, composeMessageReplyFromRecord(record));
            closeSession(socket);
        }));
    }

    const auto sharesPath = [&record, folder, &filePathRelative]() {
        const auto filePathRelativeRemote = QDir(folder->remotePath()).filePath(filePathRelative);
        // either get parent's path, or, return '/' if we are in the root folder
        auto recordPathSplit = filePathRelativeRemote.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (recordPathSplit.size() > 1) {
            recordPathSplit.removeLast();
            return recordPathSplit.join(QLatin1Char('/'));
        }
        return QStringLiteral("/");
    }();

    QMutexLocker locker(&_runningFetchShareJobsMutex);
    if (!_runningFetchShareJobsForPaths.contains(sharesPath)) {
        _runningFetchShareJobsForPaths.push_back(sharesPath);
        qInfo() << "Started OcsShareJob for path: " << sharesPath;
        job->getShares(sharesPath, {{QStringLiteral("subfiles"), QStringLiteral("true")}});
    } else {
        qInfo() << "OcsShareJob is already running for path: " << sharesPath;
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
    const QUrl jobUrl = Utility::concatUrlPath(folder->accountState()->account()->url(), getFetchThumbnailPath(), queryItems);
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

    const auto folderAlias = job->property(folderAliasPropertyKey).toString();

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

    auto objectToString = QJsonDocument(reply.object()).toJson();

    const auto sharesFetched = reply.object().value(QStringLiteral("ocs")).toObject().value(QStringLiteral("data")).toArray();

    for (const auto &share : sharesFetched) {
        const auto shareData = share.toObject();
        const auto sharePath = [&shareData, folder]() { 
            const auto sharePathRemote = shareData.value(QStringLiteral("path")).toString();

            const auto folderPath = folder->remotePath();
            if (folderPath != QLatin1Char('/') && sharePathRemote.startsWith(folderPath)) {
                // shares are ruturned with absolute remote path, so, if we have our remote root set to subfolder, we need to adjust share's remote path to relative local path
                const auto sharePathLocalRelative = sharePathRemote.midRef(folder->remotePathTrailingSlash().length());
                return sharePathLocalRelative.toString();
            }
            return sharePathRemote.size() > 1 && sharePathRemote.startsWith(QLatin1Char('/'))
                ? QString(sharePathRemote).remove(0, 1)
                : sharePathRemote;
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
    emit fetchSharesJobFinished(folderAlias);
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
