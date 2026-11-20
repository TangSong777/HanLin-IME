#include "TsfObjects.h"

#include <new>

namespace hanlin {

HINSTANCE module_instance = nullptr;
std::atomic<long> module_refs{0};

namespace {

class Factory final : public SingleInterface<IClassFactory> {
 public:
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    return outer ? CLASS_E_NOAGGREGATION : NewService(iid, out);
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock) ++module_refs;
    else --module_refs;
    return S_OK;
  }
};

class ComApartment {
 public:
  ComApartment() : status_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ComApartment() { if (SUCCEEDED(status_)) CoUninitialize(); }
  HRESULT Status() const { return status_ == RPC_E_CHANGED_MODE ? S_OK : status_; }

 private:
  HRESULT status_;
};

std::wstring ClassPath() {
  wchar_t id[40]{};
  StringFromGUID2(kTip, id, static_cast<int>(std::size(id)));
  return std::wstring(L"Software\\Classes\\CLSID\\") + id;
}

HRESULT SetString(HKEY key, const wchar_t* name, const wchar_t* value) {
  const auto size = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
  return HRESULT_FROM_WIN32(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), size));
}

HRESULT RegisterCom(const wchar_t* image) {
  const auto path = ClassPath();
  HKEY service = nullptr;
  auto result = HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
      nullptr, 0, KEY_WRITE, nullptr, &service, nullptr));
  if (FAILED(result)) return result;
  result = SetString(service, nullptr, kName);
  if (SUCCEEDED(result)) {
    HKEY server = nullptr;
    result = HRESULT_FROM_WIN32(RegCreateKeyExW(service, L"InprocServer32", 0,
        nullptr, 0, KEY_WRITE, nullptr, &server, nullptr));
    if (SUCCEEDED(result)) {
      result = SetString(server, nullptr, image);
      if (SUCCEEDED(result)) result = SetString(server, L"ThreadingModel", L"Apartment");
      RegCloseKey(server);
    }
  }
  RegCloseKey(service);
  return result;
}

HRESULT Profiles(ComPtr<ITfInputProcessorProfileMgr>& profiles, ComPtr<ITfCategoryMgr>& categories) {
  auto result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
  if (SUCCEEDED(result)) result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
  return result;
}

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

HRESULT RegisterTip() {
  wchar_t image[32768]{};
  const auto size = GetModuleFileNameW(module_instance, image, static_cast<DWORD>(std::size(image)));
  if (!size || size >= std::size(image)) return E_FAIL;
  ComApartment apartment;
  if (FAILED(apartment.Status())) return apartment.Status();

  ComPtr<ITfInputProcessorProfileMgr> profiles;
  ComPtr<ITfCategoryMgr> categories;
  auto result = Profiles(profiles, categories);
  if (FAILED(result)) return result;
  result = RegisterCom(image);
  if (FAILED(result)) return result;
  result = profiles->RegisterProfile(kTip, kLanguage, kProfile, kName,
      static_cast<ULONG>(std::size(kName) - 1), image, size, 0, nullptr, 0, FALSE, 0);
  if (SUCCEEDED(result)) {
    for (const auto* category : kCategories) {
      result = categories->RegisterCategory(kTip, *category, kTip);
      if (FAILED(result)) break;
    }
  }
  if (FAILED(result)) {
    for (const auto* category : kCategories) categories->UnregisterCategory(kTip, *category, kTip);
    profiles->UnregisterProfile(kTip, kLanguage, kProfile, 0);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClassPath().c_str());
  }
  return result;
}

HRESULT UnregisterTip() {
  ComApartment apartment;
  if (FAILED(apartment.Status())) return apartment.Status();
  ComPtr<ITfInputProcessorProfileMgr> profiles;
  ComPtr<ITfCategoryMgr> categories;
  auto result = Profiles(profiles, categories);
  if (SUCCEEDED(result)) {
    for (const auto* category : kCategories) categories->UnregisterCategory(kTip, *category, kTip);
    result = profiles->UnregisterProfile(kTip, kLanguage, kProfile, 0);
  }
  const auto removed = RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClassPath().c_str());
  if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(removed);
  return result;
}

}  // namespace
}  // namespace hanlin

BOOL WINAPI DllMain(HINSTANCE image, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) hanlin::module_instance = image;
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (clsid != hanlin::kTip) return CLASS_E_CLASSNOTAVAILABLE;
  auto* factory = new (std::nothrow) hanlin::Factory;
  if (!factory) return E_OUTOFMEMORY;
  const auto result = factory->QueryInterface(iid, out);
  factory->Release();
  return result;
}

STDAPI DllCanUnloadNow() { return hanlin::module_refs ? S_FALSE : S_OK; }
STDAPI DllRegisterServer() { return hanlin::RegisterTip(); }
STDAPI DllUnregisterServer() { return hanlin::UnregisterTip(); }
