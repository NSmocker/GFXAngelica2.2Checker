#include "GfxListReader.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::string Trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool IsCommentOrBlank(const std::string& value)
{
    const std::string trimmed = Trim(value);
    return trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';';
}

bool IsValidUtf8(const std::string& input)
{
    const auto* raw = reinterpret_cast<const unsigned char*>(input.c_str());
    std::size_t index = 0;
    while (index < input.size())
    {
        const unsigned char ch = raw[index];
        if (ch < 0x80)
        {
            ++index;
            continue;
        }

        if ((ch & 0xE0) == 0xC0)
        {
            if (index + 1 >= input.size() || (raw[index + 1] & 0xC0) != 0x80)
                return false;
            index += 2;
            continue;
        }

        if ((ch & 0xF0) == 0xE0)
        {
            if (index + 2 >= input.size() || (raw[index + 1] & 0xC0) != 0x80 || (raw[index + 2] & 0xC0) != 0x80)
                return false;
            index += 3;
            continue;
        }

        if ((ch & 0xF8) == 0xF0)
        {
            if (index + 3 >= input.size() || (raw[index + 1] & 0xC0) != 0x80 ||
                (raw[index + 2] & 0xC0) != 0x80 || (raw[index + 3] & 0xC0) != 0x80)
                return false;
            index += 4;
            continue;
        }

        return false;
    }

    return true;
}

std::string NormalizeToSystemAnsi(const std::string& input)
{
    if (input.empty())
        return {};

    std::string candidate = input;
    if (candidate.size() >= 3 && static_cast<unsigned char>(candidate[0]) == 0xEF &&
        static_cast<unsigned char>(candidate[1]) == 0xBB &&
        static_cast<unsigned char>(candidate[2]) == 0xBF)
        candidate.erase(0, 3);

    if (IsValidUtf8(candidate))
    {
        const int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, candidate.c_str(), -1, nullptr, 0);
        if (wideLen > 0)
        {
            std::wstring wide(static_cast<size_t>(wideLen - 1), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, candidate.c_str(), -1, &wide[0], wideLen) > 0)
            {
                const UINT codePages[] = {936u, CP_ACP};
                for (const UINT codePage : codePages)
                {
                    const int ansiLen = WideCharToMultiByte(codePage, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (ansiLen <= 0)
                        continue;

                    std::string decoded(static_cast<size_t>(ansiLen - 1), '\0');
                    if (WideCharToMultiByte(codePage, 0, wide.c_str(), -1, &decoded[0], ansiLen, nullptr, nullptr) > 0)
                        return decoded;
                }
            }
        }
    }

    return candidate;
}

std::string DecodeListLine(const std::string& line)
{
    std::string raw = line;
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF)
        raw.erase(0, 3);

    return NormalizeToSystemAnsi(raw);
}
}

namespace gfxchecker
{
std::vector<std::string> ReadGfxList(const std::string& listPath)
{
    std::ifstream input(listPath, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unable to open GFX list: " + listPath);

    std::vector<std::string> entries;
    std::string line;
    while (std::getline(input, line))
    {
        if (IsCommentOrBlank(line))
            continue;

        std::string path = DecodeListLine(line);
        path = Trim(path);
        if (!path.empty())
            entries.push_back(path);
    }

    return entries;
}
}
