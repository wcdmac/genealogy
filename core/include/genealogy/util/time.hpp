#pragma once

#include <string>

namespace genealogy {

// ISO 8601 UTC 时间串（yyyy-mm-ddThh:mm:ssZ），用于 created_at / updated_at。
std::string utc_now_iso();

}  // namespace genealogy
