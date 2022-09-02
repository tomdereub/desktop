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
    std::hash<std::wstring> hashFunc;
    const auto hash = hashFunc(itemPath.c_str());

    std::vector<winrt::Windows::Storage::Provider::StorageProviderItemProperty> properties;

    const auto itemPathString = winrt::to_string(itemPath);
    if (itemPathString.find(std::string(".sync_")) != std::string::npos
        || itemPathString.find(std::string(".owncloudsync.log")) != std::string::npos) {
        return winrt::single_threaded_vector(std::move(properties));
    }

    std::string iconResourceLog;

    const QVector<QPair<qint32, qint32>> listStates = {
        { 1, 0 },
        { 2, 1 }
    };

    LPTSTR strDLLPath1 = new TCHAR[_MAX_PATH];
    ::GetModuleFileName((HINSTANCE)&__ImageBase, strDLLPath1, _MAX_PATH);

    int randomStateIndex = 0;
    winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty;
    itemProperty.Id(listStates.at(randomStateIndex).first);
    itemProperty.Value(QString("Value%1").arg(listStates.at(randomStateIndex).first).toStdWString().c_str());
    itemProperty.IconResource(QString(QString::fromWCharArray(strDLLPath1) + QString(",%1")).arg(listStates.at(randomStateIndex).second).toStdWString().c_str());
    iconResourceLog = winrt::to_string(itemProperty.IconResource());
    properties.push_back(std::move(itemProperty));

    int randomStateIndex1 = 1;
    winrt::Windows::Storage::Provider::StorageProviderItemProperty itemProperty1;
    itemProperty1.Id(listStates.at(randomStateIndex1).first);
    itemProperty1.Value(QString("Value%1").arg(listStates.at(randomStateIndex1).first).toStdWString().c_str());
    itemProperty1.IconResource(QString(QString::fromWCharArray(strDLLPath1) + QString(",%1")) .arg(listStates.at(randomStateIndex1).second).toStdWString().c_str());
    iconResourceLog = winrt::to_string(itemProperty1.IconResource());
    properties.push_back(std::move(itemProperty1));

    return winrt::single_threaded_vector(std::move(properties));
}
}
