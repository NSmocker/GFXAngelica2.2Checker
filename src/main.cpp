#include "CheckerLog.h"
#include "EngineProbe.h"
#include "GfxListReader.h"

#include <Windows.h>
#include <ALog.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <locale.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr const char* version = "GFXAngelica2.2Checker 0.4.0 (orbit viewer)";

std::string ToSystemAnsi(const std::wstring& input)
{
    if (input.empty())
        return {};

    const UINT preferredCodePages[] = {936u, CP_ACP};
    for (const UINT codePage : preferredCodePages)
    {
        const int needed = WideCharToMultiByte(codePage, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            continue;

        std::string ansi(static_cast<size_t>(needed - 1), '\0');
        if (WideCharToMultiByte(codePage, 0, input.c_str(), -1, ansi.data(), needed, nullptr, nullptr) > 0)
            return ansi;
    }

    return {};
}

std::wstring NormalizeCommandLinePath(const std::wstring& input)
{
    if (input.empty())
        return {};

    std::wstring normalized = input;
    if (normalized.size() >= 2 && normalized.front() == L'"' && normalized.back() == L'"')
        normalized = normalized.substr(1, normalized.size() - 2);

    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    return normalized;
}

void PrintHelp()
{
    std::puts(version);
    std::puts("Usage:\n"
              "  GFXAngelica2.2Checker.exe --help\n"
              "  GFXAngelica2.2Checker.exe --version\n"
              "  GFXAngelica2.2Checker.exe --list <gfx-list.txt> [--root <asset-directory>]\n"
              "  GFXAngelica2.2Checker.exe --self-test --root <asset-directory>\n"
              "  GFXAngelica2.2Checker.exe --preview --root <asset-directory> --gfx <relative-or-full-gfx-path> [--output preview.png] [--width 512] [--height 512] [--frames 10]\n"
              "  GFXAngelica2.2Checker.exe --frames-out <directory> --root <asset-directory> --gfx <relative-or-full-gfx-path> [--width 512] [--height 512] [--times 100,200,...]\n"
              "  GFXAngelica2.2Checker.exe --interactive --root <asset-directory> --gfx <relative-or-full-gfx-path> [--width 512] [--height 512]\n\n"
              "Self-test validates AFI and library construction without creating a render device.\n"
              "Preview mode initializes a hidden A3DEngine viewport, renders one GFX effect to a PNG, and exits.\n"
              "The default PNG path is preview.png in the current directory; relative --output paths use that directory.\n"
              "Frames-out mode renders one PNG per --times point (internal gfx time, ms); the render target is\n"
              "cleared before every frame; camera.json and effect.json are written beside the PNGs.\n"
              "Interactive mode: middle mouse drag orbits the effect; W zooms in, S zooms out, Esc exits.\n"
              "The ImGui panel shows GFX statistics; Frame effect resets the orbit camera.\n"
              "Drop one .gfx from the asset root's gfx directory onto the interactive window to replace the current effect.\n"
              "List mode reads a .txt file of GFX paths, accepts UTF-8 or GBK/ANSI files, and prints the normalized entries.\n"
              "Exit codes: 0 success, 1 runtime failure, 2 invalid command line.");
}

std::string GetExecutableDirectory()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        throw std::runtime_error("Executable path is unavailable or too long for Angelica.");

    const std::wstring executable(path, length);
    const auto separator = executable.find_last_of(L"\\/");
    if (separator == std::wstring::npos || separator + 32 >= MAX_PATH)
        throw std::runtime_error("Executable directory is too long for Angelica log paths.");
    return ToSystemAnsi(executable.substr(0, separator));
}

std::wstring ValidateAssetRoot(const std::wstring& input)
{
    wchar_t fullPath[MAX_PATH]{};
    const DWORD length = GetFullPathNameW(input.c_str(), MAX_PATH, fullPath, nullptr);
    if (length == 0 || length + 32 >= MAX_PATH)
        throw std::runtime_error("Asset root is unavailable or too long for Angelica paths.");

    const std::wstring root(fullPath, length);
    const std::wstring gfxDir = root + L"\\gfx";
    const DWORD attributes = GetFileAttributesW(gfxDir.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Asset root must contain a gfx directory.");

    const std::wstring shaderFile = root + L"\\configs\\2.2\\skinmodelvs.cfg";
    const DWORD shaderAttributes = GetFileAttributesW(shaderFile.c_str());
    if (shaderAttributes == INVALID_FILE_ATTRIBUTES || (shaderAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Asset root must contain configs/2.2/skinmodelvs.cfg.");
    return root;
}

std::wstring GetAbsoluteOutputPath(const std::wstring& input)
{
    wchar_t fullPath[MAX_PATH]{};
    const DWORD length = GetFullPathNameW(input.c_str(), MAX_PATH, fullPath, nullptr);
    if (length == 0 || length >= MAX_PATH)
        throw std::runtime_error("Output path is unavailable or too long.");
    return std::wstring(fullPath, length);
}

LONG WINAPI LogUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    const auto address = reinterpret_cast<DWORD_PTR>(exceptionInfo->ExceptionRecord->ExceptionAddress);
    HMODULE faultModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &faultModule))
    {
        char modulePath[MAX_PATH]{};
        GetModuleFileNameA(faultModule, modulePath, MAX_PATH);
        a_LogOutput(1, "Unhandled exception 0x%08lX at %s+0x%IX.",
                    code, modulePath, address - reinterpret_cast<DWORD_PTR>(faultModule));
    }
    else
    {
        a_LogOutput(1, "Unhandled exception 0x%08lX at 0x%p.",
                    code, exceptionInfo->ExceptionRecord->ExceptionAddress);
    }

    void* frames[16]{};
    const USHORT frameCount = CaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
    for (USHORT i = 0; i < frameCount; ++i)
    {
        const auto frameAddress = reinterpret_cast<DWORD_PTR>(frames[i]);
        HMODULE frameModule = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frameAddress), &frameModule))
        {
            char modulePath[MAX_PATH]{};
            GetModuleFileNameA(frameModule, modulePath, MAX_PATH);
            a_LogOutput(1, "Stack[%u]: %s+0x%IX.", i, modulePath,
                        frameAddress - reinterpret_cast<DWORD_PTR>(frameModule));
        }
        else
            a_LogOutput(1, "Stack[%u]: 0x%p.", i, frames[i]);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
}

int wmain(int argc, wchar_t* argv[])
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // GBK потрібний лише для символів. Числа у GFX завжди мають десяткову крапку:
    // GfxCommon/A3DGFXEx.cpp:534-537 та A3DGFXElement.cpp читають їх через sscanf.
    // LC_ALL=".936" залишає регіональний роздільник (наприклад, кому в uk-UA).
    setlocale(LC_CTYPE, ".936");
    setlocale(LC_NUMERIC, "C");
    SetConsoleCP(936);
    SetConsoleOutputCP(936);
    if (GetACP() != 936)
        std::fprintf(stderr, "Warning: asset names are GBK, but the current Windows ANSI code page is %d. Preview requires a GBK-localized environment for Chinese asset paths.\n", GetACP());
    if (argc == 1 || (argc == 2 && std::wstring(argv[1]) == L"--help"))
    {
        PrintHelp();
        return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--version")
    {
        std::puts(version);
        return 0;
    }

    bool selfTest = false;
    bool preview = false;
    bool interactive = false;
    bool framesSpecified = false;
    std::wstring root;
    std::wstring gfxPath;
    std::wstring outputPath;
    std::wstring listPath;
    std::wstring timesText;
    std::wstring framesDirectory;
    unsigned width = 512;
    unsigned height = 512;
    unsigned frames = 10;

    if (argc >= 2 && std::wstring(argv[1]).size() > 4 &&
        std::wstring(argv[1]).rfind(L".txt", std::wstring::npos) != std::wstring::npos &&
        argv[1][0] != L'-')
        listPath = NormalizeCommandLinePath(argv[1]);

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        if (argument == L"--self-test")
            selfTest = true;
        else if (argument == L"--preview")
            preview = true;
        else if (argument == L"--interactive")
            interactive = true;
        else if (argument == L"--list" && listPath.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            listPath = NormalizeCommandLinePath(argv[++i]);
        else if (argument == L"--root" && root.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            root = NormalizeCommandLinePath(argv[++i]);
        else if (argument == L"--gfx" && gfxPath.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            gfxPath = NormalizeCommandLinePath(argv[++i]);
        else if (argument == L"--output" && outputPath.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            outputPath = NormalizeCommandLinePath(argv[++i]);
        else if (argument == L"--width" && i + 1 < argc && argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            width = static_cast<unsigned>(std::stoul(argv[++i]));
        else if (argument == L"--height" && i + 1 < argc && argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            height = static_cast<unsigned>(std::stoul(argv[++i]));
        else if (argument == L"--frames" && i + 1 < argc && argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
        {
            frames = static_cast<unsigned>(std::stoul(argv[++i]));
            framesSpecified = true;
        }
        else if (argument == L"--frames-out" && framesDirectory.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            framesDirectory = NormalizeCommandLinePath(argv[++i]);
        else if (argument == L"--times" && timesText.empty() && i + 1 < argc &&
                 argv[i + 1][0] != L'\0' && argv[i + 1][0] != L'-')
            timesText = NormalizeCommandLinePath(argv[++i]);
        else
        {
            std::fputs("Invalid, duplicate or unsupported argument. Use --help.\n", stderr);
            return 2;
        }
    }

    if (!listPath.empty())
    {
        try
        {
            const auto entries = gfxchecker::ReadGfxList(ToSystemAnsi(listPath));
            std::printf("Loaded %zu GFX entries from %ls.\n", entries.size(), listPath.c_str());
            for (std::size_t i = 0; i < entries.size(); ++i)
                std::printf("[%zu] %s\n", i + 1, entries[i].c_str());
            return 0;
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Error: %s\n", error.what());
            return 1;
        }
    }

    if (preview && interactive)
    {
        std::fputs("--preview and --interactive are mutually exclusive.\n", stderr);
        return 2;
    }

    if (!framesDirectory.empty())
    {
        if (preview || interactive)
        {
            std::fputs("--frames-out is mutually exclusive with --preview and --interactive.\n", stderr);
            return 2;
        }
        if (root.empty() || gfxPath.empty())
        {
            std::fputs("Frames-out mode requires --root <asset-directory> and --gfx <path>. Use --help.\n", stderr);
            return 2;
        }
        if (!outputPath.empty() || framesSpecified)
        {
            std::fputs("--output and --frames are only valid with --preview.\n", stderr);
            return 2;
        }

        // Часові точки: внутрішній час GFX у мілісекундах, розділені комами.
        std::vector<unsigned> timePoints;
        if (timesText.empty())
        {
            for (unsigned point = 100; point <= 1000; point += 100)
                timePoints.push_back(point);
        }
        else
        {
            try
            {
                std::size_t begin = 0;
                while (begin <= timesText.size())
                {
                    const std::size_t end = timesText.find(L',', begin);
                    const std::wstring token = end == std::wstring::npos
                        ? timesText.substr(begin)
                        : timesText.substr(begin, end - begin);
                    if (!token.empty())
                        timePoints.push_back(static_cast<unsigned>(std::stoul(token)));
                    if (end == std::wstring::npos)
                        break;
                    begin = end + 1;
                }
            }
            catch (const std::exception&)
            {
                std::fputs("Error: --times expects a comma-separated list of integers (ms).\n", stderr);
                return 2;
            }
        }
        if (timePoints.empty())
        {
            std::fputs("Error: --times produced no time points.\n", stderr);
            return 2;
        }
        std::sort(timePoints.begin(), timePoints.end());

        const DWORD attributes = GetFileAttributesW(framesDirectory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            std::fputs("Error: --frames-out directory does not exist; create it first.\n", stderr);
            return 2;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::fputs("Error: --frames-out must be a directory.\n", stderr);
            return 2;
        }

        try
        {
            CheckerLog log(GetExecutableDirectory());
            SetUnhandledExceptionFilter(LogUnhandledException);
            try
            {
                RunGfxFrameCapture(ToSystemAnsi(ValidateAssetRoot(root)), ToSystemAnsi(gfxPath),
                                   framesDirectory, width, height, timePoints);
                std::puts("Frame capture completed successfully.");
                return 0;
            }
            catch (const std::exception& error)
            {
                a_LogOutput(1, "%s", error.what());
                throw;
            }
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Error: %s\n", error.what());
            return 1;
        }
    }

    if (preview || interactive)
    {
        if (root.empty() || gfxPath.empty())
        {
            std::fputs("Preview modes require --root <asset-directory> and --gfx <path>. Use --help.\n", stderr);
            return 2;
        }
        if (interactive && (!outputPath.empty() || framesSpecified))
        {
            std::fputs("--output and --frames are only valid with --preview.\n", stderr);
            return 2;
        }
        try
        {
            CheckerLog log(GetExecutableDirectory());
            if (!interactive)
            {
                if (outputPath.empty())
                    outputPath = L"preview.png";
                outputPath = GetAbsoluteOutputPath(outputPath);
            }
            SetUnhandledExceptionFilter(LogUnhandledException);
            try
            {
                RunGfxPreview(ToSystemAnsi(ValidateAssetRoot(root)), ToSystemAnsi(gfxPath),
                              outputPath, width, height, frames, interactive);
                std::puts(interactive
                    ? "Interactive preview closed successfully."
                    : "Preview completed successfully; PNG saved.");
                return 0;
            }
            catch (const std::exception& error)
            {
                a_LogOutput(1, "%s", error.what());
                throw;
            }
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Error: %s\n", error.what());
            return 1;
        }
    }

    if (!selfTest || root.empty())
    {
        std::fputs("Expected --self-test and --root <asset-directory>. Use --help.\n", stderr);
        return 2;
    }

    try
    {
        CheckerLog log(GetExecutableDirectory());
        SetUnhandledExceptionFilter(LogUnhandledException);
        try
        {
            RunEngineProbe(ToSystemAnsi(ValidateAssetRoot(root)));
            std::puts("Self-test passed: library construction and AFI. Rendering not tested.");
            return 0;
        }
        catch (const std::exception& error)
        {
            a_LogOutput(1, "%s", error.what());
            throw;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }
}
