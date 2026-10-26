#pragma once
#include <windows.h>
#include <string>
#include <functional>

namespace gui::webview_host {

using MessageHandler = std::function<void(const std::string& jsonMessage)>;
using FileDropHandler = std::function<void(const std::wstring& filePath)>;

// Check if Microsoft Edge WebView2 Evergreen Runtime is available on this machine
bool IsWebView2RuntimeAvailable();

// Initialize WebView2 inside the given parent HWND and load the HTML document
bool Initialize(HWND parentHwnd,
                const std::string& htmlContent,
                MessageHandler onMessage,
                FileDropHandler onFileDrop);

// Resize the WebView2 controller to fill the parent window client area
void OnResize(HWND parentHwnd);

// Notify WebView2 when parent window moves
void OnMove();

// Send a UTF-8 JSON message to the frontend (received via window.chrome.webview.addEventListener('message'))
void PostJsonMessage(const std::string& jsonUtf8);

// Execute arbitrary JavaScript in the WebView2 context
void ExecuteScript(const std::wstring& script);

// Clean up COM pointers and WebView2 controller
void Shutdown();

// Returns true if WebView2 was initialized (or is initializing)
bool IsActive();

} // namespace gui::webview_host
