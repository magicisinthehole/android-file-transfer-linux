#ifndef AFTL_MTP_MTPZ_CONSTANTS_H
#define AFTL_MTP_MTPZ_CONSTANTS_H

#include <cstddef>
#include <cstdint>

namespace mtp {
namespace mtpz {

    // RSA Constants
    constexpr size_t RSA_MODULUS_SIZE = 128; // 1024 bits
    constexpr unsigned long RSA_EXPONENT = 65537; // 0x10001
    
    // Protocol Constants
    constexpr size_t HASH_SIZE = 20; // SHA-1 digest length
    constexpr size_t KEY_DERIVATION_CONST = 107;
    constexpr size_t MESSAGE_HEADER_SIZE = 156;
    
    // Markers
    constexpr uint8_t MARKER_EXPONENT_LO = 0x01;
    constexpr uint8_t MARKER_EXPONENT_HI = 0x00;
    constexpr uint8_t MARKER_SIZE = 0x80;
    
    // Message Tags
    constexpr uint8_t TAG_CERTIFICATE_MSG[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
    
} // namespace mtpz
} // namespace mtp

#endif // AFTL_MTP_MTPZ_CONSTANTS_H
