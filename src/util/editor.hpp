#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

std::optional<std::filesystem::path> findZedExecutable();
bool openInEditor(const std::filesystem::path& file);
bool configureZedAsDefaultEditor();
void showEditorStatus();

}  // namespace pp
