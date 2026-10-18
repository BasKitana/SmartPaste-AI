# SmartPaste AI

A Windows C++ learning project that answers copied questions through a local Ollama model and types the answer into the active application.

## Demo

[![Animated SmartPaste AI demo: copy a question, trigger Ctrl+Shift+A, and insert an answer](docs/media/SmartPaste-AI-preview.gif)](docs/media/SmartPaste-AI-demo.mp4)

**[Watch the 20-second demo](docs/media/SmartPaste-AI-demo.mp4)** · 1080p with mouse clicks and keyboard taps only; no music, humming, or vocals. The video illustrates the workflow with condensed timing. [Audio credit and reuse terms](docs/media/AUDIO-CREDITS.md).

The animated preview plays directly in this README without sound. Open the full video above for audio.

## Speed and answer accuracy

SmartPaste prioritizes speed. It uses `qwen3:8b` with thinking disabled (`"think": false`). The system prompt asks for only the final answer. If the copied input is a statement, fragment, or expression, it asks the model to infer the intended question silently.

These are instructions, not guarantees. SmartPaste does not verify answers, and the model can misinterpret questions, repeat an expression, or calculate incorrectly. In a test, it answered `101.899` for `2*sqrt(2) + 99 + 2*sqrt(5)`; the correct value is approximately `106.3006`. Check results independently, especially for math. Disabling thinking trades reasoning capability for speed. Response times depend on hardware, model loading, and question complexity.

## Requirements

- Windows 10 or later.
- Ollama installed, normally under `%LOCALAPPDATA%\Programs\Ollama`.
- The local model downloaded with `ollama run qwen3:8b` (enter `/bye` to leave the chat).
- To build: Microsoft Visual C++ Build Tools with the Windows SDK and C++17 support. VS Code debugging also requires Microsoft's C/C++ extension.

No API key or per-request cloud API payment is required for this local configuration. You still need sufficient disk space and memory for the model.

## Build (first time, or after code changes)

1. Open the project from an x64 Visual Studio Developer Command Prompt so `cl.exe` is available. For VS Code, run `code .` from that prompt.
2. Create a `build` folder in the project root if it does not exist.
3. In VS Code, press Ctrl+Shift+B and select **Build SmartPaste AI**. The executable is `build/main.exe`.

For debugging, select **Debug SmartPaste AI** and press F5. Only one instance can run; quit the tray instance before debugging or rebuilding. The executable is generated locally; it is not included in this source repository.

## Start the program

After building, launch SmartPaste once per Windows session:

1. Open the project folder in **Windows File Explorer**.
2. Open **build** and double-click **main.exe**. No terminal is required.
3. Look for the SmartPaste icon in the system tray, possibly under **hidden icons** (`^`). No main window opens.
4. Copy a question with **Ctrl+C**, click a text field in Notepad, then press **Ctrl+Shift+A** and release the keys. Keep the field selected while the answer generates.

SmartPaste stays running in the tray; you do not need to launch it for every question. It starts Ollama on demand if installed in the default location. The model must already be downloaded. Launch SmartPaste again after restarting Windows; automatic startup is not configured.

To quit, right-click its tray icon and choose **Quit SmartPaste AI**. After editing the prompt or code, quit, rebuild, and start the new executable.

Alternatively, run one of these commands **from the project folder**:

PowerShell:

```powershell
.\build\main.exe
```

WSL/Bash with Windows executable interoperability enabled:

```bash
./build/main.exe
```

## Use

1. Copy a question using Ctrl+C.
2. Click the destination text field. Start with Windows Notepad.
3. Press Ctrl+Shift+A and release the keys. SmartPaste reads the clipboard, generates an answer, saves it in memory, and attempts to type it into the application active when generation finishes.
4. Press Ctrl+Shift+A again with the same clipboard text to insert the saved answer without another model request.

Changing clipboard text starts a new generation. The cache holds one successful question/answer pair and lasts until exit. Extra shortcut presses during generation are ignored. Clipboard contents are preserved. Failed generation inserts nothing; the next press retries. Tray notifications report status and errors, subject to Windows notification settings.

The console is hidden during normal operation. Find SmartPaste in the system tray, possibly under hidden icons. Right-click its icon to quit. Quitting through the menu is disabled while a request or insertion is active.

Ollama starts hidden on demand if it is unavailable at `127.0.0.1:11434`. Startup is checked for roughly 15 seconds; generation has a 180-second read timeout. Ollama remains running after SmartPaste exits. Requests ask it to keep the model loaded for 10 minutes. No automatic model download, model warmup, or launch at Windows sign-in is configured.

## Input limitations

SmartPaste attempts Unicode keyboard input; it does not confirm that a text field accepted the answer. Unsupported or elevated applications may reject input. A retry can duplicate text or repeat a partial insertion. Clicking outside an editable field is not detected, and the original text field is not restored.

The answer follows the active application when generation finishes. Do not switch to a sensitive destination while waiting. Newlines simulate Return, which can submit messages or execute commands in a terminal. This MVP does not guarantee support for every Windows app.

## Edit the prompt

The system prompt is inside `generate()` in `main.cpp`. After editing, save, quit the running tray instance, rebuild, and launch again. Source edits do not change an already running executable.

## Troubleshooting

If the shortcut does nothing, check that `main.exe` is running, the tray icon exists, and another app has not claimed Ctrl+Shift+A. Try left Ctrl + left Shift + A in Windows Notepad. Check `build/smartpaste.log` for hotkey, request, and input-delivery events. The log does not deliberately record question or answer contents, but errors may contain diagnostic details.

Diagnostic commands do not read the clipboard or inject keyboard input:

```powershell
.\build\main.exe --check-ollama
.\build\main.exe --ask "What is 2 + 2? Give only the answer."
```

A missing-model error requires downloading `qwen3:8b`. If automatic startup fails, open Ollama manually and retry.

## Source and dependencies

- `main.cpp`: clipboard access, encoding conversion, Ollama startup and requests, hotkey handling, cache, tray notifications, and Unicode insertion.
- `include/httplib.h`: vendored cpp-httplib 0.61.0 for HTTP, from https://github.com/yhirose/cpp-httplib.
- `include/json.hpp`: vendored nlohmann/json 3.12.0 for JSON, from https://github.com/nlohmann/json.
- `.vscode/`: MSVC build task and debugger configuration.
- `build/`: generated files and diagnostic logs, excluded from Git.

Both third-party libraries identify their license as MIT. Their license notices are retained in the vendored headers.

## Validation

Verified during development: MSVC compilation, a real Ollama request, shortcut-triggered insertion into a Windows test text box, cached repeat insertion, and clipboard preservation. This does not establish universal application compatibility or mathematical correctness.
