#pragma once
#include "model.h"
#include <string>
#include <vector>
namespace mixer {
std::vector<std::uint8_t> renderPanel(const Interaction& interaction, bool confirmQuit = false, bool mock = false);
std::vector<std::uint8_t> renderIcon(int size);
void writePpm(const std::string& path, const std::vector<std::uint8_t>& rgba, int width, int height);
}
