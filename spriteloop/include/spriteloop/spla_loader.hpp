#pragma once

#include "spriteloop/spla_package.hpp"
#include "spriteloop/spla_result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace spriteloop {

SplaResult<SplaPackage> load_manifest_from_string(std::string_view manifest_json);
SplaResult<SplaPackage> load_package_from_file(const std::string& path);
SplaResult<SplaPackage> load_package_from_memory(const std::uint8_t* data,
                                                 std::size_t size,
                                                 std::string_view debug_name = {});
SplaResult<SplaPackage> load_package_from_memory(const std::vector<std::uint8_t>& data,
                                                 std::string_view debug_name = {});

} // namespace spriteloop
