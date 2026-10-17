# SmartPaste AI

A Windows C++ clipboard prototype that reads Unicode text from the clipboard and prints it to the console.

## Build and run

Requires Windows and the Microsoft C++ build tools. From a Visual Studio Developer PowerShell in the project folder:

```powershell
New-Item -ItemType Directory -Force build
cl.exe /EHsc /nologo /Febuild\main.exe main.cpp User32.lib
.\build\main.exe
```

Copy some text before running. VS Code build and debug configurations are included in `.vscode`.

The current prototype reads clipboard text; AI functionality has not been implemented yet.
