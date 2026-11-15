#pragma once

#include <string>

void RunEngineProbe(const std::string& assetRoot);
void RunGfxPreview(const std::string& assetRoot,
                   const std::string& gfxPath,
                   const std::wstring& outputPath,
                   unsigned width = 512,
                   unsigned height = 512,
                   unsigned frames = 10,
                   bool interactive = false);
