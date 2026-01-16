#pragma once
#include <vector>
#include <cstddef>
#include <span>
#include <utility>

#include "hakoniwa/pdu/endpoint.hpp"
#include "hakoniwa/pdu/endpoint_types.hpp"
#include "pdu_convertor.hpp"

namespace hakoniwa::pdu {

template<typename CppType, typename Convertor>
class TypedEndpoint
{
public:
    TypedEndpoint(hakoniwa::pdu::Endpoint& endpoint, hakoniwa::pdu::PduKey key)
        : ep_(endpoint), key_(std::move(key))
    {}

    HakoPduErrorType send(const CppType& value) noexcept
    {
        ensure_capacity_();
        if (buf_.empty()) return HAKO_PDU_ERR_UNSUPPORTED;

        CppType tmp = value;
        int written = conv_.cpp2pdu(tmp,
                                   reinterpret_cast<char*>(buf_.data()),
                                   static_cast<int>(buf_.size()));
        if (written <= 0) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        return ep_.send(key_, std::span<const std::byte>(buf_.data(), static_cast<size_t>(written)));
    }

    HakoPduErrorType recv(CppType& out) noexcept
    {
        ensure_capacity_();
        if (buf_.empty()) return HAKO_PDU_ERR_UNSUPPORTED;

        size_t received = 0;
        auto rc = ep_.recv(key_, std::span<std::byte>(buf_.data(), buf_.size()), received);
        if (rc != HAKO_PDU_ERR_OK) return rc;


        if (!conv_.pdu2cpp(reinterpret_cast<char*>(buf_.data()), out)) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        return HAKO_PDU_ERR_OK;
    }

private:
    hakoniwa::pdu::Endpoint& ep_;
    hakoniwa::pdu::PduKey key_;
    std::vector<std::byte> buf_;
    hako::pdu::PduConvertor<CppType, Convertor> conv_;

    void ensure_capacity_()
    {
        if (!buf_.empty()) return;

        const size_t cap = ep_.get_pdu_size(key_);
        if (cap == 0) {
            return;
        }
        buf_.assign(cap, std::byte{0});
    }
};

} // namespace hako::pdu
