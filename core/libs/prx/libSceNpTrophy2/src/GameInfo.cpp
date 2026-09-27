#include <cstddef>
#include <cstring>

#include "prx/libSceNpTrophy2/include/NpTrophy2.hpp"
#include "prx/libc/include/General.hpp"

namespace {
// Why stored, never called: offline never unlocks, so the callback never fires.
void* g_unlockCb = nullptr;
void* g_unlockArg = nullptr;
}

extern "C" {

int APS5_VABI sceNpTrophy2GetGameInfo(int context, int handle, NpTrophy2GameDetails* details, NpTrophy2GameData* data) noexcept {
 if (details == nullptr || data == nullptr) {
  return SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT;
 }
 (void)context;
 (void)handle;
 std::memset(details, 0, sizeof(*details));
 details->num_groups = NP_TROPHY2_NUM_GROUPS;
 details->num_trophies = NP_TROPHY2_NUM_TROPHIES;
 details->num_platinum = NP_TROPHY2_NUM_PLATINUM;
 details->num_gold = NP_TROPHY2_NUM_GOLD;
 details->num_silver = NP_TROPHY2_NUM_SILVER;
 details->num_bronze = NP_TROPHY2_NUM_BRONZE;
 std::strncpy(details->title, NP_TROPHY2_GAME_TITLE, sizeof(details->title) - 1);
 std::memset(data, 0, sizeof(*data));
 data->unlocked_trophies = NP_TROPHY2_UNLOCKED_TROPHIES;
 data->unlocked_platinum = NP_TROPHY2_UNLOCKED_PLATINUM;
 data->unlocked_gold = NP_TROPHY2_UNLOCKED_GOLD;
 data->unlocked_silver = NP_TROPHY2_UNLOCKED_SILVER;
 data->unlocked_bronze = NP_TROPHY2_UNLOCKED_BRONZE;
 data->progress_percentage = NP_TROPHY2_PROGRESS_PERCENTAGE;
 return SCE_NP_TROPHY2_OK;
}

int APS5_VABI sceNpTrophy2GetGameIcon(int context, int handle, void* buffer, size_t* size) noexcept {
 (void)context;
 (void)handle;
 (void)buffer;
 if (size != nullptr) {
  *size = NP_TROPHY2_ICON_SIZE_NONE;
 }
 // Why not-found, not throw: a missing icon is a real console error; the
 // title stays on its offline path instead of unwinding through guest catch.
 return SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND;
}

int APS5_VABI sceNpTrophy2RegisterUnlockCallback(void* callback, void* userdata) noexcept {
 g_unlockCb = callback;
 g_unlockArg = userdata;
 return SCE_NP_TROPHY2_OK;
}

}
