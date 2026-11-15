#include "CheckerLog.h"

#include <ALog.h>
#include <cstdio>
#include <mutex>
#include <stdexcept>

namespace
{
FILE* logFile = nullptr;
LPFNDEFLOGOUTPUT previousOutput = nullptr;
std::mutex logMutex;

void WriteEngineLog(const char* message)
{
    std::lock_guard<std::mutex> guard(logMutex);
    if (logFile)
    {
        // Зберігаємо журнал ASCII, екрануючи байти повідомлень рушія.
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(message); *p; ++p)
        {
            if (*p < 128)
                std::fputc(*p, logFile);
            else
                std::fprintf(logFile, "\\x%02X", static_cast<unsigned>(*p));
        }
        std::fflush(logFile);
    }
}
}

CheckerLog::CheckerLog(const std::string& executableDirectory)
{
    const std::string logPath = executableDirectory + "\\checker.log";
    if (fopen_s(&logFile, logPath.c_str(), "wb") != 0)
        throw std::runtime_error("Cannot create checker.log next to the executable.");

    // Джерело: PW_ClientSource/SDK/AngelicaCommon/Source/ALog.cpp:85,184.
    // Внутрішні AF.log/A3D.log також залишаються біля exe, а не в ассетах.
    ALog::SetLogDir(executableDirectory.c_str());
    previousOutput = a_RedirectDefLogOutput(&WriteEngineLog);
    a_LogOutput(0, "GFXAngelica2.2Checker started.");
}

CheckerLog::~CheckerLog()
{
    a_LogOutput(0, "GFXAngelica2.2Checker finished.");
    a_RedirectDefLogOutput(previousOutput);
    std::lock_guard<std::mutex> guard(logMutex);
    std::fclose(logFile);
    logFile = nullptr;
}
