// core/libs/prx/libScePlayGo/PlayGoInternal.hpp
// Internal helpers of libScePlayGo shared with its unit tests. Not guest-visible.

#ifndef CORE_LIBS_PRX_LIBSCEPLAYGO_PLAYGOINTERNAL_HPP
#define CORE_LIBS_PRX_LIBSCEPLAYGO_PLAYGOINTERNAL_HPP

#include <cstdint>
#include <set>
#include <string>

/**
 * @brief Extracts the chunk-id set from the text of a title's playgo-chunkdefs.xml.
 * @param xml File contents; no schema validation, malformed input only yields fewer ids.
 * @return Ids of every `chunk id="N"` element and of the range 0..N named by `default_chunk="N"`,
 *         always including 0. Ids above 65535 are ignored.
 */
std::set<std::uint16_t> PlayGoParseChunkDefs(const std::string& xml);

#endif
