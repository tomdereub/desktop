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

#ifndef SHAREEMODEL_H
#define SHAREEMODEL_H

#include <QObject>
#include <QFlags>
#include <QAbstractListModel>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QVariant>
#include <QSharedPointer>
#include <QVector>
#include <QTimer>

#include "accountfwd.h"
#include "sharee.h"

class QJsonDocument;
class QJsonObject;

namespace OCC {

Q_DECLARE_LOGGING_CATEGORY(lcSharing)

class ShareeModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(AccountState* accountState READ accountState WRITE setAccountState NOTIFY accountStateChanged)
    Q_PROPERTY(bool shareItemIsFolder READ shareItemIsFolder WRITE setShareItemIsFolder NOTIFY shareItemIsFolderChanged)
    Q_PROPERTY(QString searchString READ searchString WRITE setSearchString NOTIFY searchStringChanged)
    Q_PROPERTY(bool fetchOngoing READ fetchOngoing NOTIFY fetchOngoingChanged)
    Q_PROPERTY(LookupMode lookupMode READ lookupMode WRITE setLookupMode NOTIFY lookupModeChanged)

public:
    enum class LookupMode {
        LocalSearch = 0,
        GlobalSearch = 1
    };
    Q_ENUM(LookupMode);

    enum Roles {
        ShareeRole = Qt::UserRole + 1,
        AutoCompleterStringMatchRole,
    };
    Q_ENUM(Roles);

    explicit ShareeModel(QObject *parent = nullptr);

    using ShareeSet = QVector<ShareePtr>; // FIXME: make it a QSet<Sharee> when Sharee can be compared

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, const int role) const override;

    AccountState *accountState() const;
    bool shareItemIsFolder() const;
    QString searchString() const;
    bool fetchOngoing() const;
    LookupMode lookupMode() const;

signals:
    void accountStateChanged();
    void shareItemIsFolderChanged();
    void searchStringChanged();
    void fetchOngoingChanged();
    void lookupModeChanged();

    void shareesReady();
    void displayErrorMessage(int code, const QString &);

public slots:
    void setAccountState(AccountState *accountState);
    void setShareItemIsFolder(const bool shareItemIsFolder);
    void setSearchString(const QString &searchString);
    void setLookupMode(const LookupMode lookupMode);

    void fetch();

private slots:
     void shareesFetched(const QJsonDocument &reply);
     void setNewSharees(const QVector<ShareePtr> &newSharees);

private:
    ShareePtr parseSharee(const QJsonObject &data) const;

    QTimer _userStoppedTypingTimer;

    AccountStatePtr _accountState;
    QString _searchString;
    bool _shareItemIsFolder = false;
    bool _fetchOngoing = false;
    LookupMode _lookupMode = LookupMode::LocalSearch;

    QVector<ShareePtr> _sharees;
    QVector<ShareePtr> _shareeBlacklist;
};

}

#endif // SHAREEMODEL_H
