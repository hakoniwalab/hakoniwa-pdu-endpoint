#include "hakoniwa/pdu/pdu_factory.hpp"

int main()
{
    auto comm = hakoniwa::pdu::create_pdu_comm("package-consumer-smoke.json");
    return comm ? 0 : 0;
}
