#pragma once

#include "genesis/auth/MicrosoftAuth.hpp"

namespace genesis::ui {

void draw_device_code_modal(const auth::DeviceCodeInfo& info, bool& open);

}
