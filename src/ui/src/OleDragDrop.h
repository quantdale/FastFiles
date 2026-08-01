#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include <objidl.h>

namespace ffui {

using FileDropHandler = std::function<void(std::vector<std::wstring>, DWORD)>;
using DropDestinationProvider = std::function<std::wstring()>;

HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, IDataObject** object);
DWORD BeginFileDrag(const std::vector<std::wstring>& paths);
DWORD DetermineDropEffect(DWORD keyState, DWORD allowedEffects, const std::vector<std::wstring>& sourcePaths,
                          const std::wstring& destination);
HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler, IDropTarget** target);

} // namespace ffui
