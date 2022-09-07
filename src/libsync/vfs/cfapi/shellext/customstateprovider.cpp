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

#include "customstateprovider.h"
#include "customstateprovideripc.h"
#include <Shlguid.h>
#include <string>
#include <QVector>
#include <QRandomGenerator>

namespace winrt::CfApiShellExtensions::implementation {

winrt::Windows::Foundation::Collections::IIterable<winrt::Windows::Storage::Provider::StorageProviderItemProperty>
CustomStateProvider::GetItemProperties(hstring const &itemPath)
{
    std::vector<winrt::Windows::Storage::Provider::StorageProviderItemProperty> properties;

    if (_dllFilePath.isEmpty()) {
        return winrt::single_threaded_vector(std::move(properties));
    }

    const auto itemPathString = QString::fromStdString(winrt::to_string(itemPath));

    const auto isItemPathValid = [&itemPathString]() {
        if (itemPathString.isEmpty()) {
            return false;
        }

        const auto itemPathSplit = itemPathString.split(QStringLiteral("\\"), Qt::SkipEmptyParts);

        if (itemPathSplit.size() > 0) {
            const auto itemName = itemPathSplit.last();
            return !itemName.startsWith(QStringLiteral(".sync_")) && !itemName.startsWith(QStringLiteral(".owncloudsync.log"));
        }

        return true;
    }();

    if (!isItemPathValid) {
        return winrt::single_threaded_vector(std::move(properties));
    }

    VfsShellExtensions::CustomStateProviderIpc customStateProviderIpc;

    const auto states = customStateProviderIpc.fetchCustomStatesForFile(itemPathString);

    const auto isShared = states.value(QStringLiteral("isShared")).toBool();
    const auto isLocked = states.value(QStringLiteral("isLocked")).toBool();

    if (!isShared && !isLocked) {
        return winrt::single_threaded_vector(std::move(properties));
    }

    if (isLocked) {
        winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty;
        itemProperty.Id(1);
        itemProperty.Value(L"Value1");
        itemProperty.IconResource(_dllFilePath.toStdWString() + L",0");
        properties.push_back(std::move(itemProperty));
    }

    if (isShared) {
        winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty;
        itemProperty.Id(2);
        itemProperty.Value(L"Value2");
        itemProperty.IconResource(_dllFilePath.toStdWString() + L",1");
        properties.push_back(std::move(itemProperty));
    }

    return winrt::single_threaded_vector(std::move(properties));
}
void CustomStateProvider::setDllFilePath(LPCTSTR dllFilePath)
{
    _dllFilePath = QString::fromWCharArray(dllFilePath);
    if (!_dllFilePath.endsWith(QStringLiteral(".dll"))) {
        _dllFilePath.clear();
    }
}

QString CustomStateProvider::_dllFilePath;
}
