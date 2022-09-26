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

#include "sharemodel.h"

#include "account.h"
#include "folderman.h"
#include "theme.h"
#include "wordlist.h"

#include <QFileInfo>
#include <QTimeZone>

namespace {

static const QString placeholderLinkShareId = QStringLiteral("__placeholderLinkShareId__");

QString createRandomPassword()
{
    const auto words = OCC::WordList::getRandomWords(10);

    const auto addFirstLetter = [](const QString &current, const QString &next) -> QString {
        return current + next.at(0);
    };

    return std::accumulate(std::cbegin(words), std::cend(words), QString(), addFirstLetter);
}
}

namespace OCC {

Q_LOGGING_CATEGORY(lcShareModel, "com.nextcloud.sharemodel")

ShareModel::ShareModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// ---------------------- QAbstractListModel methods ---------------------- //

int ShareModel::rowCount(const QModelIndex &parent) const
{
    if(parent.isValid() || !_accountState || _localPath.isEmpty()) {
        return 0;
    }

    return _shares.count();
}

QHash<int, QByteArray> ShareModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles[ShareRole] = "share";
    roles[ShareTypeRole] = "shareType";
    roles[ShareIdRole] = "shareId";
    roles[IconUrlRole] = "iconUrl";
    roles[AvatarUrlRole] = "avatarUrl";
    roles[LinkRole] = "link";
    roles[LinkShareNameRole] = "linkShareName";
    roles[LinkShareLabelRole] = "linkShareLabel";
    roles[NoteEnabledRole] = "noteEnabled";
    roles[NoteRole] = "note";
    roles[ExpireDateEnabledRole] = "expireDateEnabled";
    roles[ExpireDateEnabledRole] = "expireDateEnforced";
    roles[ExpireDateRole] = "expireDate";
    roles[EnforcedMaximumExpireDateRole] = "enforcedMaximumExpireDate";
    roles[PasswordProtectEnabledRole] = "passwordProtectEnabled";
    roles[PasswordRole] = "password";
    roles[PasswordEnforcedRole] = "passwordEnforced";
    roles[EditingAllowedRole] = "editingAllowed";

    return roles;
}

QVariant ShareModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto share = _shares.at(index.row());

    if (!share) {
        return {};
    }

    // Some roles only provide values for the link and user/group share types
    if(const auto linkShare = share.objectCast<LinkShare>()) {
        switch(role) {
        case LinkRole:
            return linkShare->getLink();
        case LinkShareNameRole:
            return linkShare->getName();
        case LinkShareLabelRole:
            return linkShare->getLabel();
        case NoteEnabledRole:
            return !linkShare->getNote().isEmpty();
        case NoteRole:
            return linkShare->getNote();
        case ExpireDateEnabledRole:
            return linkShare->getExpireDate().isValid();
        case ExpireDateRole:
        {
            const auto startOfExpireDayUTC = linkShare->getExpireDate().startOfDay(QTimeZone::utc());
            return startOfExpireDayUTC.toMSecsSinceEpoch();
        }
        default:
            break;
        }

    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        switch(role) {
        case NoteEnabledRole:
            return !userGroupShare->getNote().isEmpty();
        case NoteRole:
            return userGroupShare->getNote();
        case ExpireDateEnabledRole:
            return userGroupShare->getExpireDate().isValid();
        case ExpireDateRole:
        {
            const auto startOfExpireDayUTC = userGroupShare->getExpireDate().startOfDay(QTimeZone::utc());
            return startOfExpireDayUTC.toMSecsSinceEpoch();
        }
        default:
            break;
        }
    }

    switch(role) {
    case Qt::DisplayRole:
        return displayStringForShare(share);
    case ShareRole:
        return QVariant::fromValue(share);
    case ShareTypeRole:
        return share->getShareType();
    case ShareIdRole:
        return share->getId();
    case IconUrlRole:
        return iconUrlForShare(share);
    case AvatarUrlRole:
        return avatarUrlForShare(share);
    case ExpireDateEnforcedRole:
        return expireDateEnforcedForShare(share);
    case EnforcedMaximumExpireDateRole:
        return enforcedMaxExpireDateForShare(share);
    case PasswordProtectEnabledRole:
        return share->isPasswordSet();
    case PasswordRole:
        if (!share->isPasswordSet() || !_shareIdRecentlySetPasswords.contains(share->getId())) {
            return {};
        }
        return _shareIdRecentlySetPasswords.value(share->getId());
    case PasswordEnforcedRole:
        return share->getShareType() == Share::TypeEmail &&
                _accountState &&
                _accountState->account() &&
                _accountState->account()->capabilities().isValid() &&
                _accountState->account()->capabilities().shareEmailPasswordEnforced();
    case EditingAllowedRole:
        return share->getPermissions().testFlag(SharePermissionUpdate);

    // Deal with roles that only return certain values for link or user/group share types
    case NoteEnabledRole:
    case ExpireDateEnabledRole:
        return false;
    case LinkRole:
    case LinkShareNameRole:
    case LinkShareLabelRole:
    case NoteRole:
    case ExpireDateRole:
        return {};
    default:
        qCWarning(lcShareModel) << "Got unknown role -- returning null value.";
        return {};
    }
}

// ---------------------- Internal model data methods ---------------------- //

void ShareModel::resetData()
{
    beginResetModel();

    _folder = nullptr;
    _sharePath.clear();
    _maxSharingPermissions = SharePermissions({});
    _numericFileId.clear();
    _manager.clear();
    _shares.clear();
    _fetchOngoing = false;
    _hasInitialShareFetchCompleted = false;

    Q_EMIT fetchOngoingChanged();
    Q_EMIT hasInitialShareFetchCompletedChanged();

    endResetModel();
}

void ShareModel::updateData()
{
    resetData();

    if (_localPath.isEmpty() || !_accountState || _accountState->account().isNull()) {
        qCWarning(lcShareModel) << "Not updating share model data. Local path is:"  << _localPath
                                << "Is account state null:" << !_accountState;
        return;
    }

    if (!sharingEnabled()) {
        qCWarning(lcShareModel) << "Server does not support sharing";
        return;
    }

    _folder = FolderMan::instance()->folderForPath(_localPath);

    if (!_folder) {
        qCWarning(lcShareModel) << "Could not update share model data for" << _localPath << "no responsible folder found";
        resetData();
        return;
    }

    const QString fileName = QFileInfo(_localPath).fileName();
    _sharePath = QDir(_folder->remotePath()).filePath(fileName);

    const QString file = _localPath.mid(_folder->cleanPath().length() + 1);
    SyncJournalFileRecord fileRecord;
    bool resharingAllowed = true; // lets assume the good

    if (_folder->journalDb()->getFileRecord(file, &fileRecord) &&
        fileRecord.isValid() &&
        !fileRecord._remotePerm.isNull() &&
        !fileRecord._remotePerm.hasPermission(RemotePermissions::CanReshare)) {

        resharingAllowed = false;
    }

    _maxSharingPermissions = resharingAllowed ? SharePermissions(_accountState->account()->capabilities().shareDefaultPermissions()) : SharePermissions({});
    Q_EMIT sharePermissionsChanged();

    _numericFileId = fileRecord.numericFileId();

    _placeholderLinkShare.reset(new Share(_accountState->account(),
                                          placeholderLinkShareId,
                                          _accountState->account()->id(),
                                          _accountState->account()->davDisplayName(),
                                          _localPath,
                                          Share::TypePlaceholderLink));
    slotAddShare(_placeholderLinkShare);

    auto job = new PropfindJob(_accountState->account(), _sharePath);
    job->setProperties(
        QList<QByteArray>()
        << "https://open-collaboration-services.org/ns:share-permissions"
        << "https://owncloud.org/ns:fileid" // numeric file id for fallback private link generation
        << "https://owncloud.org/ns:privatelink");
    job->setTimeout(10 * 1000);
    connect(job, &PropfindJob::result, this, &ShareModel::slotPropfindReceived);
    connect(job, &PropfindJob::finishedWithError, this, [&]{
        qCWarning(lcShareModel) << "Propfind for" << _sharePath << "failed";
        _fetchOngoing = false;
        Q_EMIT fetchOngoingChanged();
    });

    _fetchOngoing = true;
    Q_EMIT fetchOngoingChanged();
    job->start();

    initShareManager();
}

void ShareModel::initShareManager()
{
    if (!_accountState || _accountState->account().isNull()) {
        return;
    }

    bool sharingPossible = true;
    if (!publicLinkSharesEnabled()) {
        qCWarning(lcSharing) << "Link shares have been disabled";
        sharingPossible = false;
    } else if (!canShare()) {
        qCWarning(lcSharing) << "The file cannot be shared because it does not have sharing permission.";
        sharingPossible = false;
    }

    if (_manager.isNull() && sharingPossible) {
        _manager.reset(new ShareManager(_accountState->account(), this));
        connect(_manager.data(), &ShareManager::sharesFetched, this, &ShareModel::slotSharesFetched);
        connect(_manager.data(), &ShareManager::shareCreated, this, [&]{ _manager->fetchShares(_sharePath); });
        connect(_manager.data(), &ShareManager::linkShareCreated, this, &ShareModel::slotAddShare);
        connect(_manager.data(), &ShareManager::linkShareRequiresPassword, this, &ShareModel::requestPasswordForLinkShare);

        _manager->fetchShares(_sharePath);
    }
}

void ShareModel::slotPropfindReceived(const QVariantMap &result)
{
    _fetchOngoing = false;
    Q_EMIT fetchOngoingChanged();

    const QVariant receivedPermissions = result["share-permissions"];
    if (!receivedPermissions.toString().isEmpty()) {
        _maxSharingPermissions = static_cast<SharePermissions>(receivedPermissions.toInt());
        Q_EMIT sharePermissionsChanged();
        qCInfo(lcShareModel) << "Received sharing permissions for" << _sharePath << _maxSharingPermissions;
    }

    const auto privateLinkUrl = result["privatelink"].toString();
    const auto numericFileId = result["fileid"].toByteArray();

    if (!privateLinkUrl.isEmpty()) {
        qCInfo(lcShareModel) << "Received private link url for" << _sharePath << privateLinkUrl;
        _privateLinkUrl = privateLinkUrl;
    } else if (!numericFileId.isEmpty()) {
        qCInfo(lcShareModel) << "Received numeric file id for" << _sharePath << numericFileId;
        _privateLinkUrl = _accountState->account()->deprecatedPrivateLinkUrl(numericFileId).toString(QUrl::FullyEncoded);
    }
}

void ShareModel::slotSharesFetched(const QList<SharePtr> &shares)
{
    _hasInitialShareFetchCompleted = true;
    Q_EMIT hasInitialShareFetchCompletedChanged();

    qCInfo(lcSharing) << "Fetched" << shares.count() << "shares";

    for (const auto &share : shares) {
        if (share.isNull() ||
            share->account().isNull() ||
            share->getUidOwner() != share->account()->davUser()) {

            continue;
        }

        slotAddShare(share);
    }
}

void ShareModel::slotAddShare(const SharePtr &share)
{
    if (share.isNull()) {
        return;
    }

    const auto shareId = share->getId();

    // Remove placeholder link share if this is a link share
    if(share->getShareType() == Share::TypeLink) {
        slotRemoveShareWithId(placeholderLinkShareId);
    }

    QModelIndex shareModelIndex;

    if (_shareIdIndexHash.contains(shareId)) {
        const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
        const auto shareIndex = sharePersistentModelIndex.row();

        _shares.replace(shareIndex, share);

        shareModelIndex = index(sharePersistentModelIndex.row());
        Q_EMIT dataChanged(shareModelIndex, shareModelIndex);
    } else {
        const auto shareIndex = _shares.count();

        beginInsertRows({}, _shares.count(), _shares.count());
        _shares.append(share);
        endInsertRows();

        shareModelIndex = index(shareIndex);
    }

    const QPersistentModelIndex sharePersistentIndex(shareModelIndex);
    _shareIdIndexHash.insert(shareId, sharePersistentIndex);

    connect(share.data(), &Share::serverError, this, &ShareModel::slotServerError);
    connect(share.data(), &Share::passwordSetError, this, [this, shareId](const int code, const QString &message) {
        _shareIdRecentlySetPasswords.remove(shareId);
        slotServerError(code, message);
        slotSharePasswordSet(shareId);
        Q_EMIT passwordSetError(shareId);
    });

    // Passing shareId by reference here will cause crashing, so we pass by value
    connect(share.data(), &Share::shareDeleted, this, [this, shareId]{ slotRemoveShareWithId(shareId); });
    connect(share.data(), &Share::permissionsSet, this, [this, shareId]{ slotSharePermissionsSet(shareId); });
    connect(share.data(), &Share::passwordSet, this, [this, shareId]{ slotSharePasswordSet(shareId); });

    if (const auto linkShare = share.objectCast<LinkShare>()) {
        connect(linkShare.data(), &LinkShare::noteSet, this, [this, shareId]{ slotShareNoteSet(shareId); });
        connect(linkShare.data(), &LinkShare::nameSet, this, [this, shareId]{ slotShareNameSet(shareId); });
        connect(linkShare.data(), &LinkShare::labelSet, this, [this, shareId]{ slotShareLabelSet(shareId); });
        connect(linkShare.data(), &LinkShare::expireDateSet, this, [this, shareId]{ slotShareExpireDateSet(shareId); });
    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        connect(userGroupShare.data(), &UserGroupShare::noteSet, this, [this, shareId]{ slotShareNoteSet(shareId); });
        connect(userGroupShare.data(), &UserGroupShare::expireDateSet, this, [this, shareId]{ slotShareExpireDateSet(shareId); });
    }

    if (_manager) {
        connect(_manager.data(), &ShareManager::serverError, this, &ShareModel::slotServerError);
    }
}

void ShareModel::slotRemoveShareWithId(const QString &shareId)
{
    if (_shares.empty() || shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    _shareIdRecentlySetPasswords.remove(shareId);
    const auto shareIndex = _shareIdIndexHash.take(shareId);

    if (!shareIndex.isValid()) {
        qCWarning(lcShareModel) << "Won't remove share with id:" << shareId
                                << ", invalid share index: " << shareIndex;
        return;
    }

    const auto shareType = shareIndex.data(ShareTypeRole).value<Share::ShareType>();

    beginRemoveRows({}, shareIndex.row(), shareIndex.row());
    _shares.removeAt(shareIndex.row());
    endRemoveRows();

    // If no link shares then re-add placeholder link share
    if (shareType == Share::TypeLink) {

        // Early return if we find another link share
        for(const auto &share : _shares) {
            if(share->getShareType() == Share::TypeLink) {
                return;
            }
        }

        slotAddShare(_placeholderLinkShare);
    }
}

void ShareModel::slotServerError(const int code, const QString &message)
{
    qCWarning(lcShareModel) << "Error from server" << code << message;
    Q_EMIT serverError(code, message);
}

QString ShareModel::displayStringForShare(const SharePtr &share) const
{
    if (const auto linkShare = share.objectCast<LinkShare>()) {
        const auto displayString = tr("Share link");

        if (!linkShare->getLabel().isEmpty()) {
            return QStringLiteral("%1 (%2)").arg(displayString, linkShare->getLabel());
        }

        return displayString;
    } else if (share->getShareType() == Share::TypePlaceholderLink) {
        return tr("Link share");
    } else if (share->getShareWith()) {
        return share->getShareWith()->format();
    }

    qCWarning(lcShareModel) << "Unable to provide good display string for share";
    return QStringLiteral("Share");
}

QString ShareModel::iconUrlForShare(const SharePtr &share) const
{
    const auto iconsPath = QStringLiteral("image://svgimage-custom-color/");

    switch(share->getShareType()) {
    case Share::TypePlaceholderLink:
    case Share::TypeLink:
        return QString(iconsPath + QStringLiteral("public.svg"));
    case Share::TypeEmail:
        return QString(iconsPath + QStringLiteral("email.svg"));
    case Share::TypeRoom:
        return QString(iconsPath + QStringLiteral("wizard-talk.svg"));
    case Share::TypeUser:
        return QString(iconsPath + QStringLiteral("user.svg"));
    case Share::TypeGroup:
        return QString(iconsPath + QStringLiteral("wizard-groupware.svg"));
    default:
        return {};
    }
}

QString ShareModel::avatarUrlForShare(const SharePtr &share) const
{
    if (share->getShareWith() && share->getShareWith()->type() == Sharee::User && _accountState && _accountState->account()) {
        const QString provider = QStringLiteral("image://tray-image-provider/");
        const QString userId = share->getShareWith()->shareWith();
        const QString avatarUrl = Utility::concatUrlPath(_accountState->account()->url(),
                                                         QString("remote.php/dav/avatars/%1/%2.png").arg(userId, QString::number(64))).toString();
        return QString(provider + avatarUrl);
    }

    return {};
}

long long ShareModel::enforcedMaxExpireDateForShare(const SharePtr &share) const
{
    if (!_accountState || !_accountState->account() || !_accountState->account()->capabilities().isValid()) {
        return {};
    }

    int expireDays;

    if (share->getShareType() == Share::TypeLink && _accountState->account()->capabilities().sharePublicLinkEnforceExpireDate()) {
        expireDays = _accountState->account()->capabilities().sharePublicLinkExpireDateDays();

    } else if (share->getShareType() == Share::TypeRemote && _accountState->account()->capabilities().shareRemoteEnforceExpireDate()) {
        expireDays = _accountState->account()->capabilities().shareRemoteExpireDateDays();

    } else if (share.objectCast<UserGroupShare>() &&
               share->getShareType() != Share::TypeEmail &&
               _accountState->account()->capabilities().shareInternalEnforceExpireDate()) {
        expireDays = _accountState->account()->capabilities().shareInternalExpireDateDays();

    } else {
        return {};
    }

    const auto expireDateTime = QDate::currentDate().addDays(expireDays).startOfDay(QTimeZone::utc());
    return expireDateTime.toMSecsSinceEpoch();
}

bool ShareModel::expireDateEnforcedForShare(const SharePtr &share) const
{
    if(!_accountState || !_accountState->account() || !_accountState->account()->capabilities().isValid()) {
        return false;
    }

    if (share->getShareType() == Share::TypeLink) {
        return _accountState->account()->capabilities().sharePublicLinkEnforceExpireDate();

    } else if (share->getShareType() == Share::TypeRemote) {
        return _accountState->account()->capabilities().shareRemoteEnforceExpireDate();

    } else if (share.objectCast<UserGroupShare>() && share->getShareType() != Share::TypeEmail) {
        return _accountState->account()->capabilities().shareInternalEnforceExpireDate();

    }

    return false;
}

// ----------------- Shares modified signal handling slots ----------------- //

void ShareModel::slotSharePermissionsSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { EditingAllowedRole });
}

void ShareModel::slotSharePasswordSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { PasswordProtectEnabledRole, PasswordRole });
}

void ShareModel::slotShareNoteSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { NoteEnabledRole, NoteRole });
}

void ShareModel::slotShareNameSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { LinkShareNameRole });
}

void ShareModel::slotShareLabelSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { Qt::DisplayRole, LinkShareLabelRole });
}

void ShareModel::slotShareExpireDateSet(const QString &shareId)
{
    if (shareId.isEmpty() || !_shareIdIndexHash.contains(shareId)) {
        return;
    }

    const auto sharePersistentModelIndex = _shareIdIndexHash.value(shareId);
    const auto shareModelIndex = index(sharePersistentModelIndex.row());
    Q_EMIT dataChanged(shareModelIndex, shareModelIndex, { ExpireDateEnabledRole, ExpireDateRole });
}

// ----------------------- Shares modification slots ----------------------- //

void ShareModel::toggleShareAllowEditing(const SharePtr &share, const bool enable) const
{
    if (share.isNull()) {
        return;
    }

    auto permissions = share->getPermissions();
    enable ? permissions |= SharePermissionUpdate : permissions &= ~SharePermissionUpdate;

    share->setPermissions(permissions);
}

void ShareModel::toggleShareAllowEditingFromQml(const QVariant &share, const bool enable) const
{
    const auto ptr = share.value<SharePtr>();
    toggleShareAllowEditing(ptr, enable);
}

void ShareModel::toggleShareAllowResharing(const SharePtr &share, const bool enable) const
{
    if (share.isNull()) {
        return;
    }

    auto permissions = share->getPermissions();
    enable ? permissions |= SharePermissionShare : permissions &= ~SharePermissionShare;

    share->setPermissions(permissions);
}

void ShareModel::toggleShareAllowResharingFromQml(const QVariant &share, const bool enable) const
{
    const auto ptr = share.value<SharePtr>();
    toggleShareAllowResharing(ptr, enable);
}

void ShareModel::toggleSharePasswordProtect(const SharePtr &share, const bool enable)
{
    if (share.isNull()) {
        return;
    }

    if(!enable) {
        share->setPassword({});
        return;
    }

    const auto randomPassword = createRandomPassword();
    _shareIdRecentlySetPasswords.insert(share->getId(), randomPassword);
    share->setPassword(randomPassword);
}

void ShareModel::toggleSharePasswordProtectFromQml(const QVariant &share, const bool enable)
{
    const auto ptr = share.value<SharePtr>();
    toggleSharePasswordProtect(ptr, enable);
}

void ShareModel::toggleShareExpirationDate(const SharePtr &share, const bool enable) const
{
    if (share.isNull()) {
        return;
    }

    const QDate expireDate = enable ? QDate::currentDate().addDays(1) : QDate();

    if (const auto linkShare = share.objectCast<LinkShare>()) {
        linkShare->setExpireDate(expireDate);
    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        userGroupShare->setExpireDate(expireDate);
    }
}

void ShareModel::toggleShareExpirationDateFromQml(const QVariant &share, const bool enable) const
{
    const auto ptr = share.value<SharePtr>();
    toggleShareExpirationDate(ptr, enable);
}

void ShareModel::toggleShareNoteToRecipient(const SharePtr &share, const bool enable) const
{
    if (share.isNull()) {
        return;
    }

    const QString note = enable ? tr("Enter a note for the recipient") : QString();
    if (const auto linkShare = share.objectCast<LinkShare>()) {
        linkShare->setNote(note);
    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        userGroupShare->setNote(note);
    }
}

void ShareModel::toggleShareNoteToRecipientFromQml(const QVariant &share, const bool enable) const
{
    const auto ptr = share.value<SharePtr>();
    toggleShareNoteToRecipient(ptr, enable);
}

void ShareModel::setLinkShareLabel(const QSharedPointer<LinkShare> &linkShare, const QString &label) const
{
    if (linkShare.isNull()) {
        return;
    }

    linkShare->setLabel(label);
}

void ShareModel::setLinkShareLabelFromQml(const QVariant &linkShare, const QString &label) const
{
    // All of our internal share pointers are SharePtr, so cast to LinkShare for this method
    const auto ptr = linkShare.value<SharePtr>().objectCast<LinkShare>();
    setLinkShareLabel(ptr, label);
}

void ShareModel::setShareExpireDate(const SharePtr &share, const qint64 milliseconds) const
{
    if (share.isNull()) {
        return;
    }

    const auto date = QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::utc()).date();

    if (const auto linkShare = share.objectCast<LinkShare>()) {
        linkShare->setExpireDate(date);
    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        userGroupShare->setExpireDate(date);
    }
}

void ShareModel::setShareExpireDateFromQml(const QVariant &share, const QVariant milliseconds) const
{
    const auto ptr = share.value<SharePtr>();
    const auto millisecondsLL = milliseconds.toLongLong();
    setShareExpireDate(ptr, millisecondsLL);
}

void ShareModel::setSharePassword(const SharePtr &share, const QString &password)
{
    if (share.isNull()) {
        return;
    }

    _shareIdRecentlySetPasswords.insert(share->getId(), password);
    share->setPassword(password);
}

void ShareModel::setSharePasswordFromQml(const QVariant &share, const QString &password)
{
    const auto ptr = share.value<SharePtr>();
    setSharePassword(ptr, password);
}

void ShareModel::setShareNote(const SharePtr &share, const QString &note) const
{
    if (share.isNull()) {
        return;
    }

    if (const auto linkShare = share.objectCast<LinkShare>()) {
        linkShare->setNote(note);
    } else if (const auto userGroupShare = share.objectCast<UserGroupShare>()) {
        userGroupShare->setNote(note);
    }
}

void ShareModel::setShareNoteFromQml(const QVariant &share, const QString &note) const
{
    const auto ptr = share.value<SharePtr>();
    setShareNote(ptr, note);
}

// ------------------- Share creation and deletion slots ------------------- //

void ShareModel::createNewLinkShare() const
{
    if (_manager) {
        const auto askOptionalPassword = _accountState->account()->capabilities().sharePublicLinkAskOptionalPassword();
        const auto password = askOptionalPassword ? createRandomPassword() : QString();
        _manager->createLinkShare(_sharePath, QString(), password);
    }
}

void ShareModel::createNewLinkShareWithPassword(const QString &password) const
{
    if (_manager) {
        _manager->createLinkShare(_sharePath, QString(), password);
    }
}

void ShareModel::createNewUserGroupShare(const ShareePtr &sharee)
{
    if (sharee.isNull()) {
        return;
    }

    qCInfo(lcShareModel) << "Creating new user/group share for sharee: " << sharee->format();

    if (sharee->type() == Sharee::Email &&
        _accountState &&
        !_accountState->account().isNull() &&
        _accountState->account()->capabilities().isValid() &&
        _accountState->account()->capabilities().shareEmailPasswordEnforced()) {

        Q_EMIT requestPasswordForEmailSharee(sharee);
        return;
    }

    _manager->createShare(_sharePath,
                          Share::ShareType(sharee->type()),
                          sharee->shareWith(),
                          _maxSharingPermissions,
                          {});
}

void ShareModel::createNewUserGroupShareWithPassword(const ShareePtr &sharee, const QString &password) const
{
    if (sharee.isNull()) {
        return;
    }

    _manager->createShare(_sharePath,
                          Share::ShareType(sharee->type()),
                          sharee->shareWith(),
                          _maxSharingPermissions,
                          password);
}

void ShareModel::createNewUserGroupShareFromQml(const QVariant &sharee)
{
    const auto ptr = sharee.value<ShareePtr>();
    createNewUserGroupShare(ptr);
}

void ShareModel::createNewUserGroupShareWithPasswordFromQml(const QVariant &sharee, const QString &password) const
{
    const auto ptr = sharee.value<ShareePtr>();
    createNewUserGroupShareWithPassword(ptr, password);
}

void ShareModel::deleteShare(const SharePtr &share) const
{
    if(share.isNull()) {
        return;
    }

    share->deleteShare();
}

void ShareModel::deleteShareFromQml(const QVariant &share) const
{
    const auto ptr = share.value<SharePtr>();
    deleteShare(ptr);
}

// --------------------------- QPROPERTY methods --------------------------- //

QString ShareModel::localPath() const
{
    return _localPath;
}

void ShareModel::setLocalPath(const QString &localPath)
{
    _localPath = localPath;
    Q_EMIT localPathChanged();
    updateData();
}

AccountState *ShareModel::accountState() const
{
    return _accountState;
}

void ShareModel::setAccountState(AccountState *accountState)
{
    _accountState = accountState;

    // Change the server and account-related properties
    connect(_accountState, &AccountState::stateChanged, this, &ShareModel::accountConnectedChanged);
    connect(_accountState, &AccountState::stateChanged, this, &ShareModel::sharingEnabledChanged);
    connect(_accountState, &AccountState::stateChanged, this, &ShareModel::publicLinkSharesEnabledChanged);
    connect(_accountState, &AccountState::stateChanged, this, &ShareModel::userGroupSharingEnabledChanged);

    Q_EMIT accountStateChanged();
    Q_EMIT accountConnectedChanged();
    Q_EMIT sharingEnabledChanged();
    Q_EMIT publicLinkSharesEnabledChanged();
    Q_EMIT userGroupSharingEnabledChanged();
    updateData();
}

bool ShareModel::accountConnected() const
{
    return _accountState && _accountState->isConnected();
}

bool ShareModel::sharingEnabled() const
{
    return _accountState &&
            _accountState->account() &&
            _accountState->account()->capabilities().isValid() &&
            _accountState->account()->capabilities().shareAPI();
}

bool ShareModel::publicLinkSharesEnabled() const
{
    return Theme::instance()->linkSharing() &&
            _accountState &&
            _accountState->account() &&
            _accountState->account()->capabilities().isValid() &&
            _accountState->account()->capabilities().sharePublicLink();
}

bool ShareModel::userGroupSharingEnabled() const
{
    return Theme::instance()->userGroupSharing();
}

bool ShareModel::fetchOngoing() const
{
    return _fetchOngoing;
}

bool ShareModel::hasInitialShareFetchCompleted() const
{
    return _hasInitialShareFetchCompleted;
}

bool ShareModel::canShare() const
{
    return _maxSharingPermissions & SharePermissionShare;
}

} // namespace OCC
