#pragma once

#include <string>
#include <vector>

void RunEngineProbe(const std::string& assetRoot);
void RunGfxPreview(const std::string& assetRoot,
                   const std::string& gfxPath,
                   const std::wstring& outputPath,
                   unsigned width = 512,
                   unsigned height = 512,
                   unsigned frames = 10,
                   bool interactive = false);

// Покадровий захоплювач: по одному PNG на кожен цільовий внутрішній час (мс) GFX,
// render target очищається перед кожним кадром. Камера — за формулою AABB
// (ConfigureCameraFromAabb); eye/target/FOV зберігаються у camera.json для Unity.
void RunGfxFrameCapture(const std::string& assetRoot,
                        const std::string& gfxPath,
                        const std::wstring& framesDirectory,
                        unsigned width,
                        unsigned height,
                        const std::vector<unsigned>& timePointsMs);
