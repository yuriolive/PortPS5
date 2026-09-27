#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpCommerceDialogUpdateStatus(void) noexcept {
 // Why OK: offline commerce has no store; the dialog finishes immediately so
 // boot never blocks on a purchase prompt.
 return 0;
}

}
