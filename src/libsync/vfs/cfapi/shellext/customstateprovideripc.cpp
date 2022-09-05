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

#include "customstateprovideripc.h"
#include "common/shellextensionutils.h"
#include "common/utility.h"
#include <QString>
#include <QSize>
#include <QtNetwork/QLocalSocket>
#include <QJsonDocument>
#include <QObject>
#include <QDir>
#include <Windows.h>
namespace {
// we don't want to block the Explorer for too long (default is 30K, so we'd keep it at 10K, except QLocalSocket::waitForDisconnected())
constexpr auto socketTimeoutMs = 60000;
}

namespace VfsShellExtensions {

CustomStateProviderIpc::CustomStateProviderIpc()
{
    _localSocket.reset(new QLocalSocket());
}
CustomStateProviderIpc::~CustomStateProviderIpc()
{
    disconnectSocketFromServer();
}

QVariantMap CustomStateProviderIpc::fetchCustomStatesForFile(const QString &filePath)
{
    const auto sendMessageAndReadyRead = [this](QVariantMap &message) {
        _localSocket->write(VfsShellExtensions::Protocol::createJsonMessage(message));
        return _localSocket->waitForBytesWritten(socketTimeoutMs) && _localSocket->waitForReadyRead(socketTimeoutMs);
    };

    const auto mainServerName = getServerNameForPath(filePath);

    if (mainServerName.isEmpty()) {
        return {};
    }

    // #1 Connect to the local server
    if (!connectSocketToServer(mainServerName)) {
        return {};
    }

    auto messageRequestCustomStatesForFile = QVariantMap {
        {
            VfsShellExtensions::Protocol::CustomStateProviderRequestKey,
            QVariantMap {
                { VfsShellExtensions::Protocol::FilePathKey, filePath }
            }
        }
    };

    // #2 Request custom states for a 'filePath'
    if (!sendMessageAndReadyRead(messageRequestCustomStatesForFile)) {
        return {};
    }

    // #3 Receive custom states as JSON
    const auto message = QJsonDocument::fromJson(_localSocket->readAll()).toVariant().toMap();
    if (!VfsShellExtensions::Protocol::validateProtocolVersion(message) || !message.contains(VfsShellExtensions::Protocol::CustomStateDataKey)) {
        return {};
    }
    const auto customStates = message.value(VfsShellExtensions::Protocol::CustomStateDataKey).toMap();
    disconnectSocketFromServer();

    return customStates;
}

bool CustomStateProviderIpc::disconnectSocketFromServer()
{
    const auto isConnectedOrConnecting = _localSocket->state() == QLocalSocket::ConnectedState || _localSocket->state() == QLocalSocket::ConnectingState;
    if (isConnectedOrConnecting) {
        _localSocket->disconnectFromServer();
        const auto isNotConnected = _localSocket->state() == QLocalSocket::UnconnectedState || _localSocket->state() == QLocalSocket::ClosingState;
        return isNotConnected || _localSocket->waitForDisconnected();
    }
    return true;
}

QString CustomStateProviderIpc::getServerNameForPath(const QString &filePath)
{
    if (!overrideServerName.isEmpty()) {
        return overrideServerName;
    }
    // SyncRootManager Registry key contains all registered folders for Cf API. It will give us the correct name of the current app based on the folder path
    QString serverName;
    constexpr auto syncRootManagerRegKey = R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager)";

    if (OCC::Utility::registryKeyExists(HKEY_LOCAL_MACHINE, syncRootManagerRegKey)) {
        OCC::Utility::registryWalkSubKeys(HKEY_LOCAL_MACHINE, syncRootManagerRegKey, [&](HKEY, const QString &syncRootId) {
            const QString syncRootIdUserSyncRootsRegistryKey = syncRootManagerRegKey + QStringLiteral("\\") + syncRootId + QStringLiteral(R"(\UserSyncRoots\)");
            OCC::Utility::registryWalkValues(HKEY_LOCAL_MACHINE, syncRootIdUserSyncRootsRegistryKey, [&](const QString &userSyncRootName, bool *done) {
                const auto userSyncRootValue = QDir::fromNativeSeparators(OCC::Utility::registryGetKeyValue(HKEY_LOCAL_MACHINE, syncRootIdUserSyncRootsRegistryKey, userSyncRootName).toString());
                if (QDir::fromNativeSeparators(filePath).startsWith(userSyncRootValue)) {
                    const auto syncRootIdSplit = syncRootId.split(QLatin1Char('!'), Qt::SkipEmptyParts);
                    if (!syncRootIdSplit.isEmpty()) {
                        serverName = VfsShellExtensions::serverNameForApplicationName(syncRootIdSplit.first());
                        *done = true;
                    }
                }
            });
        });
    }
    return serverName;
}

bool CustomStateProviderIpc::connectSocketToServer(const QString &serverName)
{
    if (!disconnectSocketFromServer()) {
        return false;
    }
    _localSocket->setServerName(serverName);
    _localSocket->connectToServer();
    return _localSocket->state() == QLocalSocket::ConnectedState || _localSocket->waitForConnected(socketTimeoutMs);
}
QString CustomStateProviderIpc::overrideServerName = {};
}
