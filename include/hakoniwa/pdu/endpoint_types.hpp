#pragma once

#include "hakoniwa/pdu/endpoint_types.h"
#include "hakoniwa/hako_primitive_types.h" // Added
#include <string>


namespace hakoniwa {
namespace pdu {

struct PduKey {
    std::string robot;
    std::string pdu;
};
struct PduResolvedKey {
    std::string robot;
    HakoPduChannelIdType channel_id; // Changed
};

// Common hash function and equality operator for PduResolvedKey
// for use in std::unordered_map.
struct PduResolvedKeyHash {
  std::size_t operator()(const PduResolvedKey &k) const {
    return std::hash<std::string>()(k.robot) ^
           (std::hash<HakoPduChannelIdType>()(k.channel_id) << 1); // Changed
  }
};

inline bool operator==(const PduResolvedKey &a, const PduResolvedKey &b) {
  return a.robot == b.robot && a.channel_id == b.channel_id;
}

}
} // namespace hakoniwa::pdu