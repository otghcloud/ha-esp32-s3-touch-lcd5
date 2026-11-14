#pragma once
#include "page_shared.h"

namespace wallpanel {
inline void render_homepage(lv_obj_t *title, const Homepage &home) {
  lv_label_set_text(title, home.title.c_str());
}
}  // namespace wallpanel
