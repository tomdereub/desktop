/*
 * Copyright (C) 2022 by Claudio Cambra <claudio.cambra@nextcloud.com>
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

#ifndef OCC_FILEDETAILUTILS_H
#define OCC_FILEDETAILUTILS_H

#include <QObject>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QTimer>

#include "common/syncjournalfilerecord.h"

namespace OCC {

class FileDetails : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localPath READ localPath WRITE setLocalPath NOTIFY localPathChanged)
    Q_PROPERTY(QString name READ name NOTIFY fileChanged)
    Q_PROPERTY(QString sizeString READ sizeString NOTIFY fileChanged)
    Q_PROPERTY(QString lastChangedString READ lastChangedString NOTIFY fileChanged)
    Q_PROPERTY(QString iconUrl READ iconUrl NOTIFY fileChanged)
    Q_PROPERTY(QString lockExpireString READ lockExpireString NOTIFY lockExpireStringChanged)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY isFolderChanged)

public:
    explicit FileDetails(QObject *parent = nullptr);

    QString localPath() const;
    QString name() const;
    QString sizeString() const;
    QString lastChangedString() const;
    QString iconUrl() const;
    QString lockExpireString() const;
    bool isFolder() const;

public slots:
    void setLocalPath(const QString &localPath);

signals:
    void localPathChanged();
    void fileChanged();
    void lockExpireStringChanged();
    void isFolderChanged();

private slots:
    void refreshFileDetails();
    void updateLockExpireString();

private:
    QString _localPath;

    QFileInfo _fileInfo;
    QFileSystemWatcher _fileWatcher;
    SyncJournalFileRecord _fileRecord;
    SyncJournalFileLockInfo _filelockState;
    QByteArray _numericFileId;
    QString _lockExpireString;
    QTimer _filelockStateUpdateTimer;

    QLocale _locale;
};

} // namespace OCC

#endif // OCC_FILEDETAILUTILS_H
