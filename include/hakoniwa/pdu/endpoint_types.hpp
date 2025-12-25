#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include <string>


namespace hakoniwa {
namespace pdu {

struct PduKey {
    std::string robot;
    std::string pdu;
};
struct PduResolvedKey {
    std::string robot;
    hako_pdu_uint32_t channel_id;
};

// Common hash function and equality operator for PduResolvedKey
// for use in std::unordered_map.
struct PduResolvedKeyHash {
  std::size_t operator()(const PduResolvedKey &k) const {
    return std::hash<std::string>()(k.robot) ^
           (std::hash<hako_pdu_uint32_t>()(k.channel_id) << 1);
  }
};

inline bool operator==(const PduResolvedKey &a, const PduResolvedKey &b) {
  return a.robot == b.robot && a.channel_id == b.channel_id;
}

}
} // namespace hakoniwa::pdu