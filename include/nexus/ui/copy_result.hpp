#pragma once

#include "nexus/core/result.hpp"

namespace nexus::ui {

// Puts the payload of an ActionKind::Copy result on the clipboard; other kinds are ignored.
void copy_result(const core::SearchResult& res);

}  // namespace nexus::ui
