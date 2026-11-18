// スクリプトを選ぶダイアログ。Windows 標準のファイル選択ダイアログ（IFileOpenDialog）を、
// COM（Windows の部品を呼び出す仕組み）で呼び出す。実体は実行時に読み込まれるので、
// 依存 DLL は増えない。前に開いたフォルダなどは、Windows が覚えている（OS 標準の動き）。

#include "picker.h"

#include <windows.h>
#include <shobjidl.h>

#include "util.h"

bool choose_script(const std::wstring& default_dir, std::wstring& path, std::string& error) {
  ensure_com();

  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) {
    error = windows_error_message(static_cast<unsigned long>(hr));
    return false;
  }

  const COMDLG_FILTERSPEC filters[] = {
      {L"Ruby スクリプト (*.rb)", L"*.rb"},
      {L"すべてのファイル (*.*)", L"*.*"},
  };
  dialog->SetFileTypes(2, filters);
  dialog->SetFileTypeIndex(1);  // 1 から数える
  dialog->SetTitle(L"実行するスクリプトを選ぶ - Pikulix");

  // FOS_FORCEFILESYSTEM: ライブラリなどの仮想的な場所ではなく、実際のファイルに限る。
  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
  }

  // SetDefaultFolder は、Windows が前に開いたフォルダを覚えていないときだけ使われる。
  IShellItem* folder = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(default_dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
    dialog->SetDefaultFolder(folder);
    folder->Release();
  }

  bool ok = false;
  hr = dialog->Show(nullptr);
  if (SUCCEEDED(hr)) {
    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (SUCCEEDED(hr)) {
      wchar_t* name = nullptr;
      hr = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
      if (SUCCEEDED(hr)) {
        path = name;
        CoTaskMemFree(name);
        ok = true;
      }
      item->Release();
    }
  }
  dialog->Release();

  if (!ok && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    error = windows_error_message(static_cast<unsigned long>(hr));
  }
  return ok;
}
