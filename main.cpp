#include <iostream>
#include <string>
#include <Windows.h>
using namespace std;

bool checkClipBoard() {
    // check if can open ClipBoard
    if (!OpenClipboard(nullptr)) {
        cout << "Could not open clipboard." << endl;
        return false;
    }
    return true;
}

int main() {
    cout << "Welcome to SmartPaste AI." << endl;
    if (checkClipBoard() == false)
    {
        return -1;
    }
    // Request Unicode text from the open clipboard.
    HANDLE clipboardHandle = GetClipboardData(CF_UNICODETEXT);
    if (clipboardHandle == nullptr) {
        // No text handle was returned; close clipboard access before exiting.
        cout << "Can't get text." << endl;
        CloseClipboard();
        return -1;
    }
    //Lock clipboard memory
    const wchar_t* clipboardText = static_cast<const wchar_t*>(GlobalLock(clipboardHandle));
    //checking if text is available
    if (clipboardText == nullptr) {
        cout << "No text in clipboard. Please copy something to clipboard.";
        CloseClipboard();
        return -1;
    }
    // copy the clipboard text 
    const wstring copiedText(clipboardText);
    GlobalUnlock(clipboardHandle);
    CloseClipboard();
    wcout << copiedText << endl;
    return 0;
}
