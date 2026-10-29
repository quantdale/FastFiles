#pragma once

#include <windows.h>
#include <objidl.h>
#include <string>

namespace ffui {

struct NewFileWriteResult {
    HRESULT error = S_OK;
    HRESULT cleanupError = S_OK;
};

// Never opens an existing destination for write. Consumes the stream from zero.
NewFileWriteResult WriteNewFile(const std::wstring& path, IStream* stream);

}
