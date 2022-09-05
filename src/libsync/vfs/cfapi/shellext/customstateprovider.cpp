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
#include <QString>
#include <QVector>
#include <QRandomGenerator>


EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace winrt::CfApiShellExtensions::implementation {

winrt::Windows::Foundation::Collections::IIterable<winrt::Windows::Storage::Provider::StorageProviderItemProperty>
CustomStateProvider::GetItemProperties(hstring const &itemPath)
{
    VfsShellExtensions::CustomStateProviderIpc customStateProviderIpc;

    std::vector<winrt::Windows::Storage::Provider::StorageProviderItemProperty> properties;

    const auto itemPathString = winrt::to_string(itemPath);
    if (itemPathString.find(std::string(".sync_")) != std::string::npos
        || itemPathString.find(std::string(".owncloudsync.log")) != std::string::npos) {
        return winrt::single_threaded_vector(std::move(properties));
    }

    const auto states = customStateProviderIpc.fetchCustomStatesForFile(QString::fromStdString(itemPathString));

    const auto isShared = states.value(QLatin1Literal("isShared")).toBool();
    const auto isLocked = states.value(QLatin1Literal("isLocked")).toBool();

    if (!isShared && !isLocked) {
        properties.clear();
        return winrt::single_threaded_vector(std::move(properties));
    }

    LPTSTR strDLLPath1 = new TCHAR[_MAX_PATH];
    ::GetModuleFileName((HINSTANCE)&__ImageBase, strDLLPath1, _MAX_PATH);

    if (isLocked) {
        winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty;
        itemProperty.Id(1);
        itemProperty.Value(L"Value1");
        itemProperty.IconResource(QString(QString::fromWCharArray(strDLLPath1) + QString(",%1")).arg(0).toStdWString().c_str());
        properties.push_back(std::move(itemProperty));
    }

    if (isShared) {
        winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty;
        itemProperty.Id(2);
        itemProperty.Value(L"Value2");
        itemProperty.IconResource(QString(QString::fromWCharArray(strDLLPath1) + QString(",%1")).arg(1).toStdWString().c_str());
        properties.push_back(std::move(itemProperty));
    }

    return winrt::single_threaded_vector(std::move(properties));
}
}
