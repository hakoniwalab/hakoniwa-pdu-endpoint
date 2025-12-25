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

}
} // namespace hakoniwa::pdu