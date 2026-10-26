#include "webview_host.h"
#include "dllinjector/text.h"
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace gui::webview_host {

namespace {

struct EventRegistrationToken {
    int64_t value;
};

struct COREWEBVIEW2_COLOR {
    BYTE A;
    BYTE R;
    BYTE G;
    BYTE B;
};

// Explicit C-style COM vtable definitions — 100% ABI-identical on MSVC (x86/x64) and MinGW GCC
struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2Controller2;
struct ICoreWebView2;
struct ICoreWebView2Settings;
struct ICoreWebView2WebMessageReceivedEventArgs;
struct ICoreWebView2WebMessageReceivedEventArgs2;
struct ICoreWebView2ObjectCollectionView;
struct ICoreWebView2File;
struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
struct ICoreWebView2WebMessageReceivedEventHandler;

static const IID kIID_EnvironmentCompletedHandler = {
    0x4E8A3389, 0xC9D8, 0x4BD2, {0xB6, 0xB5, 0x12, 0x4F, 0xEE, 0x6C, 0xC1, 0x4D}
};
static const IID kIID_ControllerCompletedHandler = {
    0x6C4819F3, 0xC9B7, 0x4260, {0x81, 0x27, 0xC9, 0xF5, 0xBD, 0xE7, 0xF6, 0x8C}
};
static const IID kIID_WebMessageReceivedHandler = {
    0x57213F19, 0x00E6, 0x49FA, {0x8E, 07, 0x89, 0x8E, 0xA0, 0x1E, 0xCB, 0xD2}
};
static const IID kIID_ICoreWebView2Controller2 = {
    0xC979903E, 0xD4CA, 0x4228, {0x92, 0xEB, 0x47, 0xEE, 0x3F, 0xA9, 0x6E, 0xAB}
};
static const IID kIID_ICoreWebView2WebMessageReceivedEventArgs2 = {
    0x06FC7AB7, 0xC90C, 0x4297, {0x93, 0x89, 0x33, 0xCA, 0x01, 0xCF, 0x6D, 0x5E}
};
static const IID kIID_ICoreWebView2File = {
    0xF2C19559, 0x6BC1, 0x4583, {0xA7, 0x57, 0x90, 0x02, 0x1B, 0xE9, 0xAF, 0xEC}
};

struct ICoreWebView2EnvironmentCompletedHandlerVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
    HRESULT(STDMETHODCALLTYPE* Invoke)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This, HRESULT errorCode, ICoreWebView2Environment* createdEnvironment);
};
struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    const ICoreWebView2EnvironmentCompletedHandlerVtbl* lpVtbl;
};

struct ICoreWebView2ControllerCompletedHandlerVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
    HRESULT(STDMETHODCALLTYPE* Invoke)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This, HRESULT errorCode, ICoreWebView2Controller* createdController);
};
struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    const ICoreWebView2ControllerCompletedHandlerVtbl* lpVtbl;
};

struct ICoreWebView2WebMessageReceivedEventHandlerVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2WebMessageReceivedEventHandler* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2WebMessageReceivedEventHandler* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2WebMessageReceivedEventHandler* This);
    HRESULT(STDMETHODCALLTYPE* Invoke)(ICoreWebView2WebMessageReceivedEventHandler* This, ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args);
};
struct ICoreWebView2WebMessageReceivedEventHandler {
    const ICoreWebView2WebMessageReceivedEventHandlerVtbl* lpVtbl;
};

struct ICoreWebView2EnvironmentVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2Environment* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2Environment* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2Environment* This);
    HRESULT(STDMETHODCALLTYPE* CreateCoreWebView2Controller)(ICoreWebView2Environment* This, HWND parentWindow, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler);
    HRESULT(STDMETHODCALLTYPE* CreateWebResourceResponse)(ICoreWebView2Environment* This, void* content, int statusCode, LPCWSTR reasonPhrase, LPCWSTR headers, void** response);
    HRESULT(STDMETHODCALLTYPE* get_BrowserVersionString)(ICoreWebView2Environment* This, LPWSTR* versionInfo);
    HRESULT(STDMETHODCALLTYPE* add_NewBrowserVersionAvailable)(ICoreWebView2Environment* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_NewBrowserVersionAvailable)(ICoreWebView2Environment* This, EventRegistrationToken token);
};
struct ICoreWebView2Environment {
    const ICoreWebView2EnvironmentVtbl* lpVtbl;
};

struct ICoreWebView2SettingsVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2Settings* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2Settings* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2Settings* This);
    HRESULT(STDMETHODCALLTYPE* get_IsScriptEnabled)(ICoreWebView2Settings* This, BOOL* isScriptEnabled);
    HRESULT(STDMETHODCALLTYPE* put_IsScriptEnabled)(ICoreWebView2Settings* This, BOOL isScriptEnabled);
    HRESULT(STDMETHODCALLTYPE* get_IsWebMessageEnabled)(ICoreWebView2Settings* This, BOOL* isWebMessageEnabled);
    HRESULT(STDMETHODCALLTYPE* put_IsWebMessageEnabled)(ICoreWebView2Settings* This, BOOL isWebMessageEnabled);
    HRESULT(STDMETHODCALLTYPE* get_AreDefaultScriptDialogsEnabled)(ICoreWebView2Settings* This, BOOL* areDefaultScriptDialogsEnabled);
    HRESULT(STDMETHODCALLTYPE* put_AreDefaultScriptDialogsEnabled)(ICoreWebView2Settings* This, BOOL areDefaultScriptDialogsEnabled);
    HRESULT(STDMETHODCALLTYPE* get_IsStatusBarEnabled)(ICoreWebView2Settings* This, BOOL* isStatusBarEnabled);
    HRESULT(STDMETHODCALLTYPE* put_IsStatusBarEnabled)(ICoreWebView2Settings* This, BOOL isStatusBarEnabled);
    HRESULT(STDMETHODCALLTYPE* get_AreDevToolsEnabled)(ICoreWebView2Settings* This, BOOL* areDevToolsEnabled);
    HRESULT(STDMETHODCALLTYPE* put_AreDevToolsEnabled)(ICoreWebView2Settings* This, BOOL areDevToolsEnabled);
    HRESULT(STDMETHODCALLTYPE* get_AreDefaultContextMenusEnabled)(ICoreWebView2Settings* This, BOOL* enabled);
    HRESULT(STDMETHODCALLTYPE* put_AreDefaultContextMenusEnabled)(ICoreWebView2Settings* This, BOOL enabled);
    HRESULT(STDMETHODCALLTYPE* get_AreHostObjectsAllowed)(ICoreWebView2Settings* This, BOOL* allowed);
    HRESULT(STDMETHODCALLTYPE* put_AreHostObjectsAllowed)(ICoreWebView2Settings* This, BOOL allowed);
    HRESULT(STDMETHODCALLTYPE* get_IsZoomControlEnabled)(ICoreWebView2Settings* This, BOOL* enabled);
    HRESULT(STDMETHODCALLTYPE* put_IsZoomControlEnabled)(ICoreWebView2Settings* This, BOOL enabled);
    HRESULT(STDMETHODCALLTYPE* get_IsBuiltInErrorPageEnabled)(ICoreWebView2Settings* This, BOOL* enabled);
    HRESULT(STDMETHODCALLTYPE* put_IsBuiltInErrorPageEnabled)(ICoreWebView2Settings* This, BOOL enabled);
};
struct ICoreWebView2Settings {
    const ICoreWebView2SettingsVtbl* lpVtbl;
};

struct ICoreWebView2Vtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2* This);
    HRESULT(STDMETHODCALLTYPE* get_Settings)(ICoreWebView2* This, ICoreWebView2Settings** settings);
    HRESULT(STDMETHODCALLTYPE* get_Source)(ICoreWebView2* This, LPWSTR* uri);
    HRESULT(STDMETHODCALLTYPE* Navigate)(ICoreWebView2* This, LPCWSTR uri);
    HRESULT(STDMETHODCALLTYPE* NavigateToString)(ICoreWebView2* This, LPCWSTR htmlContent);
    HRESULT(STDMETHODCALLTYPE* add_NavigationStarting)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_NavigationStarting)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_ContentLoading)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_ContentLoading)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_SourceChanged)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_SourceChanged)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_HistoryChanged)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_HistoryChanged)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_NavigationCompleted)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_NavigationCompleted)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_FrameNavigationStarting)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_FrameNavigationStarting)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_FrameNavigationCompleted)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_FrameNavigationCompleted)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_ScriptDialogOpening)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_ScriptDialogOpening)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_PermissionRequested)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_PermissionRequested)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_ProcessFailed)(ICoreWebView2* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_ProcessFailed)(ICoreWebView2* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* AddScriptToExecuteOnDocumentCreated)(ICoreWebView2* This, LPCWSTR javaScript, void* handler);
    HRESULT(STDMETHODCALLTYPE* RemoveScriptToExecuteOnDocumentCreated)(ICoreWebView2* This, LPCWSTR id);
    HRESULT(STDMETHODCALLTYPE* ExecuteScript)(ICoreWebView2* This, LPCWSTR javaScript, void* handler);
    HRESULT(STDMETHODCALLTYPE* CapturePreview)(ICoreWebView2* This, int imageFormat, void* imageStream, void* handler);
    HRESULT(STDMETHODCALLTYPE* Reload)(ICoreWebView2* This);
    HRESULT(STDMETHODCALLTYPE* PostWebMessageAsJson)(ICoreWebView2* This, LPCWSTR webMessageAsJson);
    HRESULT(STDMETHODCALLTYPE* PostWebMessageAsString)(ICoreWebView2* This, LPCWSTR webMessageAsString);
    HRESULT(STDMETHODCALLTYPE* add_WebMessageReceived)(ICoreWebView2* This, ICoreWebView2WebMessageReceivedEventHandler* handler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_WebMessageReceived)(ICoreWebView2* This, EventRegistrationToken token);
};
struct ICoreWebView2 {
    const ICoreWebView2Vtbl* lpVtbl;
};

struct ICoreWebView2ControllerVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2Controller* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2Controller* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2Controller* This);
    HRESULT(STDMETHODCALLTYPE* get_IsVisible)(ICoreWebView2Controller* This, BOOL* isVisible);
    HRESULT(STDMETHODCALLTYPE* put_IsVisible)(ICoreWebView2Controller* This, BOOL isVisible);
    HRESULT(STDMETHODCALLTYPE* get_Bounds)(ICoreWebView2Controller* This, RECT* bounds);
    HRESULT(STDMETHODCALLTYPE* put_Bounds)(ICoreWebView2Controller* This, RECT bounds);
    HRESULT(STDMETHODCALLTYPE* get_ZoomFactor)(ICoreWebView2Controller* This, double* zoomFactor);
    HRESULT(STDMETHODCALLTYPE* put_ZoomFactor)(ICoreWebView2Controller* This, double zoomFactor);
    HRESULT(STDMETHODCALLTYPE* add_ZoomFactorChanged)(ICoreWebView2Controller* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_ZoomFactorChanged)(ICoreWebView2Controller* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* SetBoundsAndZoomFactor)(ICoreWebView2Controller* This, RECT bounds, double zoomFactor);
    HRESULT(STDMETHODCALLTYPE* MoveFocus)(ICoreWebView2Controller* This, int reason);
    HRESULT(STDMETHODCALLTYPE* add_MoveFocusRequested)(ICoreWebView2Controller* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_MoveFocusRequested)(ICoreWebView2Controller* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_GotFocus)(ICoreWebView2Controller* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_GotFocus)(ICoreWebView2Controller* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_LostFocus)(ICoreWebView2Controller* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_LostFocus)(ICoreWebView2Controller* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* add_AcceleratorKeyPressed)(ICoreWebView2Controller* This, void* eventHandler, EventRegistrationToken* token);
    HRESULT(STDMETHODCALLTYPE* remove_AcceleratorKeyPressed)(ICoreWebView2Controller* This, EventRegistrationToken token);
    HRESULT(STDMETHODCALLTYPE* get_ParentWindow)(ICoreWebView2Controller* This, HWND* parentWindow);
    HRESULT(STDMETHODCALLTYPE* put_ParentWindow)(ICoreWebView2Controller* This, HWND parentWindow);
    HRESULT(STDMETHODCALLTYPE* NotifyParentWindowPositionChanged)(ICoreWebView2Controller* This);
    HRESULT(STDMETHODCALLTYPE* Close)(ICoreWebView2Controller* This);
    HRESULT(STDMETHODCALLTYPE* get_CoreWebView2)(ICoreWebView2Controller* This, ICoreWebView2** coreWebView2);
};
struct ICoreWebView2Controller {
    const ICoreWebView2ControllerVtbl* lpVtbl;
};

struct ICoreWebView2Controller2Vtbl {
    ICoreWebView2ControllerVtbl base;
    HRESULT(STDMETHODCALLTYPE* get_DefaultBackgroundColor)(ICoreWebView2Controller2* This, COREWEBVIEW2_COLOR* backgroundColor);
    HRESULT(STDMETHODCALLTYPE* put_DefaultBackgroundColor)(ICoreWebView2Controller2* This, COREWEBVIEW2_COLOR backgroundColor);
};
struct ICoreWebView2Controller2 {
    const ICoreWebView2Controller2Vtbl* lpVtbl;
};

struct ICoreWebView2FileVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2File* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2File* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2File* This);
    HRESULT(STDMETHODCALLTYPE* get_Path)(ICoreWebView2File* This, LPWSTR* value);
};
struct ICoreWebView2File {
    const ICoreWebView2FileVtbl* lpVtbl;
};

struct ICoreWebView2ObjectCollectionViewVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2ObjectCollectionView* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2ObjectCollectionView* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2ObjectCollectionView* This);
    HRESULT(STDMETHODCALLTYPE* get_Count)(ICoreWebView2ObjectCollectionView* This, UINT32* value);
    HRESULT(STDMETHODCALLTYPE* GetValueAtIndex)(ICoreWebView2ObjectCollectionView* This, UINT32 index, IUnknown** value);
};
struct ICoreWebView2ObjectCollectionView {
    const ICoreWebView2ObjectCollectionViewVtbl* lpVtbl;
};

struct ICoreWebView2WebMessageReceivedEventArgsVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ICoreWebView2WebMessageReceivedEventArgs* This, REFIID riid, void** ppvObject);
    ULONG(STDMETHODCALLTYPE* AddRef)(ICoreWebView2WebMessageReceivedEventArgs* This);
    ULONG(STDMETHODCALLTYPE* Release)(ICoreWebView2WebMessageReceivedEventArgs* This);
    HRESULT(STDMETHODCALLTYPE* get_Source)(ICoreWebView2WebMessageReceivedEventArgs* This, LPWSTR* source);
    HRESULT(STDMETHODCALLTYPE* get_WebMessageAsJson)(ICoreWebView2WebMessageReceivedEventArgs* This, LPWSTR* webMessageAsJson);
    HRESULT(STDMETHODCALLTYPE* TryGetWebMessageAsString)(ICoreWebView2WebMessageReceivedEventArgs* This, LPWSTR* webMessageAsString);
};
struct ICoreWebView2WebMessageReceivedEventArgs {
    const ICoreWebView2WebMessageReceivedEventArgsVtbl* lpVtbl;
};

struct ICoreWebView2WebMessageReceivedEventArgs2Vtbl {
    ICoreWebView2WebMessageReceivedEventArgsVtbl base;
    HRESULT(STDMETHODCALLTYPE* get_AdditionalObjects)(ICoreWebView2WebMessageReceivedEventArgs2* This, ICoreWebView2ObjectCollectionView** value);
};
struct ICoreWebView2WebMessageReceivedEventArgs2 {
    const ICoreWebView2WebMessageReceivedEventArgs2Vtbl* lpVtbl;
};

using CreateWebViewEnvironmentWithOptionsInternal_t = HRESULT(STDMETHODCALLTYPE*)(
    bool checkRunningInstance,
    int runtimeType,
    PCWSTR userDataFolder,
    IUnknown* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* createdHandler);

// Global state
HWND g_parentHwnd = nullptr;
HMODULE g_ebWebViewModule = nullptr;
ICoreWebView2Environment* g_environment = nullptr;
ICoreWebView2Controller* g_controller = nullptr;
ICoreWebView2* g_webview = nullptr;
std::wstring g_initialHtmlWide;
MessageHandler g_onMessage;
FileDropHandler g_onFileDrop;
bool g_active = false;

std::wstring Utf8ToWideString(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), len);
    return out;
}

std::string WideToUtf8String(PCWSTR wstr) {
    if (!wstr || !wstr[0]) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring FindEmbeddedBrowserWebViewDll() {
    static const wchar_t* kSubKey =
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, kSubKey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
            wchar_t ebPath[MAX_PATH * 2] = {};
            DWORD cbData = sizeof(ebPath);
            DWORD type = 0;
            LONG res = RegQueryValueExW(hKey, L"EBWebView", nullptr, &type, reinterpret_cast<LPBYTE>(ebPath), &cbData);
            RegCloseKey(hKey);
            if (res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && ebPath[0] != L'\0') {
                std::wstring dllPath = ebPath;
                if (dllPath.back() != L'\\' && dllPath.back() != L'/') {
                    dllPath += L'\\';
                }
                dllPath += L"EBWebView\\";
#if defined(_M_X64) || defined(__x86_64__)
                dllPath += L"x64\\EmbeddedBrowserWebView.dll";
#elif defined(_M_IX86) || defined(__i386__)
                dllPath += L"x86\\EmbeddedBrowserWebView.dll";
#elif defined(_M_ARM64) || defined(__aarch64__)
                dllPath += L"arm64\\EmbeddedBrowserWebView.dll";
#endif
                if (GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    return dllPath;
                }
            }
        }
    }
    return L"";
}

std::wstring GetUserDataFolder() {
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) {
        return L"";
    }
    std::wstring folder = tempDir;
    if (!folder.empty() && folder.back() != L'\\') folder += L'\\';
#if defined(_WIN64)
    folder += L"DLLInjector_WebView2_x64";
#else
    folder += L"DLLInjector_WebView2_x86";
#endif
    return folder;
}

// 1. WebMessageReceivedHandler (C-style COM layout)
struct MessageReceivedHandlerImpl {
    ICoreWebView2WebMessageReceivedEventHandler iface;
    std::atomic<ULONG> refCount{1};
};

HRESULT STDMETHODCALLTYPE MsgHandler_QueryInterface(
    ICoreWebView2WebMessageReceivedEventHandler* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_WebMessageReceivedHandler)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MsgHandler_AddRef(ICoreWebView2WebMessageReceivedEventHandler* This) {
    auto* self = reinterpret_cast<MessageReceivedHandlerImpl*>(This);
    return ++self->refCount;
}

ULONG STDMETHODCALLTYPE MsgHandler_Release(ICoreWebView2WebMessageReceivedEventHandler* This) {
    auto* self = reinterpret_cast<MessageReceivedHandlerImpl*>(This);
    ULONG r = --self->refCount;
    if (r == 0) delete self;
    return r;
}

HRESULT STDMETHODCALLTYPE MsgHandler_Invoke(
    ICoreWebView2WebMessageReceivedEventHandler* /*This*/,
    ICoreWebView2* /*sender*/,
    ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (!args) return S_OK;

    ICoreWebView2WebMessageReceivedEventArgs2* args2 = nullptr;
    if (SUCCEEDED(args->lpVtbl->QueryInterface(args, kIID_ICoreWebView2WebMessageReceivedEventArgs2,
                                               reinterpret_cast<void**>(&args2))) && args2) {
        ICoreWebView2ObjectCollectionView* collection = nullptr;
        if (SUCCEEDED(args2->lpVtbl->get_AdditionalObjects(args2, &collection)) && collection) {
            UINT32 count = 0;
            if (SUCCEEDED(collection->lpVtbl->get_Count(collection, &count)) && count > 0) {
                for (UINT32 i = 0; i < count; ++i) {
                    IUnknown* obj = nullptr;
                    if (SUCCEEDED(collection->lpVtbl->GetValueAtIndex(collection, i, &obj)) && obj) {
                        ICoreWebView2File* file = nullptr;
                        if (SUCCEEDED(obj->QueryInterface(kIID_ICoreWebView2File,
                                                          reinterpret_cast<void**>(&file))) && file) {
                            LPWSTR filePath = nullptr;
                            if (SUCCEEDED(file->lpVtbl->get_Path(file, &filePath)) && filePath) {
                                std::wstring dropped(filePath);
                                CoTaskMemFree(filePath);
                                if (g_onFileDrop && !dropped.empty()) {
                                    g_onFileDrop(dropped);
                                }
                            }
                            file->lpVtbl->Release(file);
                        }
                        obj->Release();
                    }
                }
            }
            collection->lpVtbl->Release(collection);
        }
        args2->lpVtbl->base.Release(reinterpret_cast<ICoreWebView2WebMessageReceivedEventArgs*>(args2));
    }

    LPWSTR msgWide = nullptr;
    if (SUCCEEDED(args->lpVtbl->TryGetWebMessageAsString(args, &msgWide)) && msgWide) {
        std::string utf8 = WideToUtf8String(msgWide);
        CoTaskMemFree(msgWide);
        if (g_onMessage && !utf8.empty() && utf8 != "drop") {
            g_onMessage(utf8);
        }
    }
    return S_OK;
}

static const ICoreWebView2WebMessageReceivedEventHandlerVtbl kMsgHandlerVtbl = {
    MsgHandler_QueryInterface,
    MsgHandler_AddRef,
    MsgHandler_Release,
    MsgHandler_Invoke
};

// 2. ControllerCompletedHandler (C-style COM layout)
struct ControllerCompletedHandlerImpl {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler iface;
    std::atomic<ULONG> refCount{1};
};

HRESULT STDMETHODCALLTYPE CtrlHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_ControllerCompletedHandler)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CtrlHandler_AddRef(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
    auto* self = reinterpret_cast<ControllerCompletedHandlerImpl*>(This);
    return ++self->refCount;
}

ULONG STDMETHODCALLTYPE CtrlHandler_Release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
    auto* self = reinterpret_cast<ControllerCompletedHandlerImpl*>(This);
    ULONG r = --self->refCount;
    if (r == 0) delete self;
    return r;
}

HRESULT STDMETHODCALLTYPE CtrlHandler_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* /*This*/,
    HRESULT errorCode,
    ICoreWebView2Controller* createdController) {
    if (FAILED(errorCode) || !createdController) {
        return S_OK;
    }

    g_controller = createdController;
    g_controller->lpVtbl->AddRef(g_controller);

    ICoreWebView2Controller2* ctrl2 = nullptr;
    if (SUCCEEDED(g_controller->lpVtbl->QueryInterface(g_controller, kIID_ICoreWebView2Controller2,
                                                       reinterpret_cast<void**>(&ctrl2))) && ctrl2) {
        COREWEBVIEW2_COLOR bg = { 255, 28, 28, 30 };
        ctrl2->lpVtbl->put_DefaultBackgroundColor(ctrl2, bg);
        ctrl2->lpVtbl->base.Release(reinterpret_cast<ICoreWebView2Controller*>(ctrl2));
    }

    if (SUCCEEDED(g_controller->lpVtbl->get_CoreWebView2(g_controller, &g_webview)) && g_webview) {
        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(g_webview->lpVtbl->get_Settings(g_webview, &settings)) && settings) {
            settings->lpVtbl->put_IsScriptEnabled(settings, TRUE);
            settings->lpVtbl->put_IsWebMessageEnabled(settings, TRUE);
            settings->lpVtbl->put_IsStatusBarEnabled(settings, FALSE);
            settings->lpVtbl->put_AreDefaultContextMenusEnabled(settings, FALSE);
            settings->lpVtbl->put_AreDevToolsEnabled(settings, TRUE);
            settings->lpVtbl->Release(settings);
        }

        EventRegistrationToken token{};
        auto* msgHandler = new MessageReceivedHandlerImpl{{&kMsgHandlerVtbl}, 1};
        g_webview->lpVtbl->add_WebMessageReceived(g_webview, &msgHandler->iface, &token);
        msgHandler->iface.lpVtbl->Release(&msgHandler->iface);

        if (!g_initialHtmlWide.empty()) {
            g_webview->lpVtbl->NavigateToString(g_webview, g_initialHtmlWide.c_str());
        }
    }

    OnResize(g_parentHwnd);
    g_controller->lpVtbl->put_IsVisible(g_controller, TRUE);
    return S_OK;
}

static const ICoreWebView2ControllerCompletedHandlerVtbl kCtrlHandlerVtbl = {
    CtrlHandler_QueryInterface,
    CtrlHandler_AddRef,
    CtrlHandler_Release,
    CtrlHandler_Invoke
};

// 3. EnvironmentCompletedHandler (C-style COM layout)
struct EnvironmentCompletedHandlerImpl {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler iface;
    std::atomic<ULONG> refCount{1};
};

HRESULT STDMETHODCALLTYPE EnvHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_EnvironmentCompletedHandler)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE EnvHandler_AddRef(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
    auto* self = reinterpret_cast<EnvironmentCompletedHandlerImpl*>(This);
    return ++self->refCount;
}

ULONG STDMETHODCALLTYPE EnvHandler_Release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
    auto* self = reinterpret_cast<EnvironmentCompletedHandlerImpl*>(This);
    ULONG r = --self->refCount;
    if (r == 0) delete self;
    return r;
}

HRESULT STDMETHODCALLTYPE EnvHandler_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* /*This*/,
    HRESULT errorCode,
    ICoreWebView2Environment* createdEnvironment) {
    if (FAILED(errorCode) || !createdEnvironment || !g_parentHwnd) {
        return S_OK;
    }
    g_environment = createdEnvironment;
    g_environment->lpVtbl->AddRef(g_environment);

    auto* ctrlHandler = new ControllerCompletedHandlerImpl{{&kCtrlHandlerVtbl}, 1};
    g_environment->lpVtbl->CreateCoreWebView2Controller(g_environment, g_parentHwnd, &ctrlHandler->iface);
    ctrlHandler->iface.lpVtbl->Release(&ctrlHandler->iface);
    return S_OK;
}

static const ICoreWebView2EnvironmentCompletedHandlerVtbl kEnvHandlerVtbl = {
    EnvHandler_QueryInterface,
    EnvHandler_AddRef,
    EnvHandler_Release,
    EnvHandler_Invoke
};

} // namespace

bool IsWebView2RuntimeAvailable() {
    return !FindEmbeddedBrowserWebViewDll().empty();
}

bool Initialize(HWND parentHwnd,
                const std::string& htmlContent,
                MessageHandler onMessage,
                FileDropHandler onFileDrop) {
    std::wstring dllPath = FindEmbeddedBrowserWebViewDll();
    if (dllPath.empty()) {
        return false;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_ebWebViewModule = LoadLibraryW(dllPath.c_str());
    if (!g_ebWebViewModule) {
        return false;
    }

    auto createEnvFn = reinterpret_cast<CreateWebViewEnvironmentWithOptionsInternal_t>(
        GetProcAddress(g_ebWebViewModule, "CreateWebViewEnvironmentWithOptionsInternal"));
    if (!createEnvFn) {
        FreeLibrary(g_ebWebViewModule);
        g_ebWebViewModule = nullptr;
        return false;
    }

    g_parentHwnd = parentHwnd;
    g_initialHtmlWide = Utf8ToWideString(htmlContent);
    g_onMessage = std::move(onMessage);
    g_onFileDrop = std::move(onFileDrop);

    std::wstring userDataFolder = GetUserDataFolder();
    auto* envHandler = new EnvironmentCompletedHandlerImpl{{&kEnvHandlerVtbl}, 1};
    HRESULT hr = createEnvFn(true, 0 /* installed runtime */,
                             userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
                             nullptr, &envHandler->iface);
    envHandler->iface.lpVtbl->Release(&envHandler->iface);

    if (FAILED(hr)) {
        FreeLibrary(g_ebWebViewModule);
        g_ebWebViewModule = nullptr;
        return false;
    }

    g_active = true;
    return true;
}

void OnResize(HWND parentHwnd) {
    if (!g_controller || !parentHwnd) return;
    RECT rc{};
    if (GetClientRect(parentHwnd, &rc)) {
        g_controller->lpVtbl->put_Bounds(g_controller, rc);
    }
}

void OnMove() {
    if (g_controller) {
        g_controller->lpVtbl->NotifyParentWindowPositionChanged(g_controller);
    }
}

void PostJsonMessage(const std::string& jsonUtf8) {
    if (!g_webview || jsonUtf8.empty()) return;
    std::wstring wjson = Utf8ToWideString(jsonUtf8);
    if (!wjson.empty()) {
        g_webview->lpVtbl->PostWebMessageAsJson(g_webview, wjson.c_str());
    }
}

void ExecuteScript(const std::wstring& script) {
    if (!g_webview || script.empty()) return;
    g_webview->lpVtbl->ExecuteScript(g_webview, script.c_str(), nullptr);
}

void Shutdown() {
    if (!g_active) return;
    g_active = false;
    g_onMessage = nullptr;
    g_onFileDrop = nullptr;
    if (g_controller) {
        g_controller->lpVtbl->Close(g_controller);
        g_controller->lpVtbl->Release(g_controller);
        g_controller = nullptr;
    }
    if (g_webview) {
        g_webview->lpVtbl->Release(g_webview);
        g_webview = nullptr;
    }
    if (g_environment) {
        g_environment->lpVtbl->Release(g_environment);
        g_environment = nullptr;
    }
}

bool IsActive() {
    return g_active;
}

} // namespace gui::webview_host
