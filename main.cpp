#include "include/httplib.h"
#include "include/json.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <future>
#include <fstream>
#include <mutex>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Shell32.lib")
using json = nlohmann::json;
using namespace std;

constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT InsertTimer = 2;
NOTIFYICONDATAW tray{};
future<wstring> pending;
wstring activeQuestion, cachedQuestion, cachedAnswer;
bool busy = false;

void logEvent(const string& event) {
    static mutex lock;
    lock_guard<mutex> guard(lock);
    wchar_t executable[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return;
    wstring path(executable);
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"smartpaste.log";
    ofstream output(path, ios::app);
    output << GetTickCount64() << " " << event << endl;
}

void notify(const wstring& message, bool error = false) {
    logEvent(error ? "Error notification requested" : "Status notification requested");
    tray.uFlags = NIF_INFO;
    tray.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    wcsncpy_s(tray.szInfoTitle, L"SmartPaste AI", _TRUNCATE);
    wcsncpy_s(tray.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

bool ensureOllamaReady() {
    httplib::Client probe("127.0.0.1", 11434);
    probe.set_connection_timeout(1, 0);
    probe.set_read_timeout(1, 0);
    auto ready = [&]() {
        auto response = probe.Get("/api/version");
        return response && response->status == 200;
    };
    if (ready()) return true;
    wchar_t directory[32768];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, 32768);
    if (!length || length >= 32768) return false;
    wstring executable = wstring(directory) + L"\\Programs\\Ollama\\ollama.exe";
    wstring command = L"\"" + executable + L"\" serve";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ULONGLONG deadline = GetTickCount64() + 15000;
    do {
        if (ready()) return true;
        Sleep(250);
    } while (GetTickCount64() < deadline);
    return false;
}

string toUtf8(const wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw runtime_error("Invalid clipboard Unicode text.");
    string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

wstring fromUtf8(const string& text) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw runtime_error("Invalid or empty answer text.");
    wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size);
    return result;
}

wstring readClipboard() {
    if (!OpenClipboard(nullptr)) throw runtime_error("Clipboard is busy. Try again.");
    struct ClipboardScope { ~ClipboardScope() { CloseClipboard(); } } clipboardScope;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) throw runtime_error("Copy a text question first.");
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!text) throw runtime_error("Could not access clipboard text.");
    struct MemoryScope {
        HANDLE handle;
        ~MemoryScope() { GlobalUnlock(handle); }
    } memoryScope{handle};
    size_t capacity = GlobalSize(handle) / sizeof(wchar_t);
    size_t length = 0;
    while (length < capacity && text[length] != L'\0') ++length;
    if (length == capacity || length == 0 || length > 16000)
        throw runtime_error("Copy between 1 and 16000 text characters.");
    wstring result(text, length);
    if (result.find_first_not_of(L" \t\r\n") == wstring::npos)
        throw runtime_error("Clipboard contains only whitespace.");
    return result;
}

wstring generate(const wstring& question) {
    logEvent("Checking Ollama readiness");
    if (!ensureOllamaReady()) throw runtime_error("Cannot start Ollama. Install or open Ollama, then retry.");
    httplib::Client client("127.0.0.1", 11434);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(180, 0);
    client.set_write_timeout(5, 0);
    json body = {
        {"model", "qwen3:8b"}, {"stream", false}, {"think", false}, {"keep_alive", "10m"},
        {"messages", json::array({
            {{"role", "system"}, {"content", "Treat every user input as a question to answer. If it is a statement, fragment, or expression, silently infer the intended question and answer it. Output only the final answer in plain text. Never repeat, rewrite, or output the question. Do not include explanations, reasoning, preambles, or commentary. For mathematical expressions, evaluate or simplify them and output only the result. When asked for a numerical value, compute a decimal approximation instead of merely restating the expression."}},
            {{"role", "user"}, {"content", toUtf8(question)}}
        })}
    };
    logEvent("Sending generation request");
    auto response = client.Post("/api/chat", body.dump(), "application/json");
    logEvent(response ? "HTTP status " + to_string(response->status) : "HTTP failure: " + httplib::to_string(response.error()));
    if (!response) throw runtime_error("Ollama request failed: " + httplib::to_string(response.error()));
    if (response->status != 200)
        throw runtime_error("Ollama HTTP " + to_string(response->status) + ". Check that qwen3:8b is installed.");
    json answer = json::parse(response->body);
    if (!answer.value("done", false)) throw runtime_error("Ollama returned an incomplete answer.");
    string content = answer.at("message").at("content").get<string>();
    if (content.empty() || content.size() > 64000) throw runtime_error("Answer is empty or too long.");
    wstring result = fromUtf8(content);
    if (result.find(L'\0') != wstring::npos) throw runtime_error("Answer contains invalid control text.");
    return result;
}

bool modifiersDown() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

void insertAnswer() {
    vector<INPUT> inputs;
    inputs.reserve(cachedAnswer.size() * 2);
    for (wchar_t character : cachedAnswer) {
        if (character == L'\r') continue;
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        // Newlines use Return; all other characters use Unicode input.
        if (character == L'\n') down.ki.wVk = VK_RETURN;
        else { down.ki.wScan = character; down.ki.dwFlags = KEYEVENTF_UNICODE; }
        inputs.push_back(down);
        down.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(down);
    }
    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    logEvent("Input events queued: " + to_string(sent) + "/" + to_string(inputs.size()));
    if (sent != inputs.size())
        notify(L"Input was blocked or partial. Answer is saved; select a field and retry. A retry may duplicate text.", true);
    // SendInput success means queued input, not confirmed insertion into an editable field.
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_HOTKEY && wParam == 1) {
        logEvent(busy ? "Hotkey received while busy" : "Hotkey received");
        if (busy) return 0;
        try {
            wstring question = readClipboard();
            logEvent("Clipboard text read successfully");
            busy = true;
            if (question == cachedQuestion && !cachedAnswer.empty()) {
                SetTimer(window, InsertTimer, 50, nullptr);
            } else {
                activeQuestion = question;
                pending = async(launch::async, [question]() { return generate(question); });
                notify(L"Generating answer. It will type into the active app when ready.");
            }
        } catch (const exception& error) {
            logEvent(string("Workflow error: ") + error.what());
            busy = false;
            notify(fromUtf8(error.what()), true);
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == 1 && pending.valid() &&
        pending.wait_for(chrono::seconds(0)) == future_status::ready) {
        try {
            wstring answer = pending.get();
            cachedQuestion = activeQuestion;
            cachedAnswer = move(answer);
            SetTimer(window, InsertTimer, 50, nullptr);
        } catch (const exception& error) {
            logEvent(string("Generation error: ") + error.what());
            busy = false;
            notify(fromUtf8(error.what()), true);
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == InsertTimer) {
        if (!modifiersDown()) {
            KillTimer(window, InsertTimer);
            insertAnswer();
            busy = false;
        }
        return 0;
    }
    if (message == TrayMessage && (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (busy ? MF_GRAYED : 0), 1, L"Quit SmartPaste AI");
        POINT position;
        GetCursorPos(&position);
        SetForegroundWindow(window);
        int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            position.x, position.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice == 1) DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        UnregisterHotKey(window, 1);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int main(int argc, char* argv[]) {
    // Diagnostic commands avoid hotkeys, clipboard changes, and simulated input.
    if (argc > 1) {
        try {
            string option = argv[1];
            if (option == "--check-ollama") {
                if (!ensureOllamaReady()) throw runtime_error("Ollama startup failed.");
                cout << "Ollama is ready." << endl;
            } else if (option == "--ask" && argc == 3) {
                cout << toUtf8(generate(fromUtf8(argv[2]))) << endl;
            } else throw runtime_error("Usage: main.exe [--check-ollama | --ask \"question\"]");
            return 0;
        } catch (const exception& error) { cerr << error.what() << endl; return 1; }
    }
    HANDLE instance = CreateMutexW(nullptr, FALSE, L"Local\\SmartPasteAI.MVP");
    if (!instance || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"SmartPaste AI is already running, or could not start.", L"SmartPaste AI", MB_OK);
        if (instance) CloseHandle(instance);
        return 1;
    }
    HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = module;
    windowClass.lpszClassName = L"SmartPasteAI.Window";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowW(windowClass.lpszClassName, L"SmartPaste AI", 0,
        0, 0, 0, 0, nullptr, nullptr, module, nullptr);
    if (!window || !RegisterHotKey(window, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'A')) {
        MessageBoxW(nullptr, L"Cannot register Ctrl+Shift+A. Another program may own it.", L"SmartPaste AI", MB_OK | MB_ICONERROR);
        if (window) DestroyWindow(window);
        CloseHandle(instance);
        return 1;
    }
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(tray.szTip, L"SmartPaste AI - Ctrl+Shift+A; right-click to quit");
    if (!Shell_NotifyIconW(NIM_ADD, &tray)) {
        MessageBoxW(nullptr, L"Cannot create tray icon.", L"SmartPaste AI", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        CloseHandle(instance);
        return 1;
    }
    SetTimer(window, 1, 100, nullptr);
    logEvent("Started; Ctrl+Shift+A registered");
    notify(L"Ready. Copy a question, select a text field, press Ctrl+Shift+A. Right-click tray icon to quit.");
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(instance);
    return 0;
}
