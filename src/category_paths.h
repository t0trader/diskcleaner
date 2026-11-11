#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace diskcleaner {

// 返回某清理类别对应的实际存在路径集合（已展开环境变量与通配符，并去重）。
// 扫描与清理共用同一份定义，避免两处路径表漂移。
std::vector<std::wstring> GetCategoryPaths(CleanupCategory category);

} // namespace diskcleaner
