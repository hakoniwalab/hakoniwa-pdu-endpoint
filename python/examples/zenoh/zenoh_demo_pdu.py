def make_uint16_payload(value: int, pdu_size: int) -> bytes:
    from hakoniwa_pdu.pdu_msgs.std_msgs.pdu_conv_UInt16 import py_to_pdu_UInt16
    from hakoniwa_pdu.pdu_msgs.std_msgs.pdu_pytype_UInt16 import UInt16

    msg = UInt16()
    msg.data = value & 0xFFFF
    payload = bytes(py_to_pdu_UInt16(msg))
    if len(payload) > pdu_size:
        raise ValueError(f"UInt16 PDU payload is larger than configured pdu_size: {len(payload)} > {pdu_size}")
    return payload.ljust(pdu_size, b"\x00")


def read_uint16_payload(payload: bytes) -> int:
    from hakoniwa_pdu.pdu_msgs.std_msgs.pdu_conv_UInt16 import pdu_to_py_UInt16

    return int(pdu_to_py_UInt16(bytearray(payload)).data)
