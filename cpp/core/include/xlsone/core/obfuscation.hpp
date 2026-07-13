#pragma once

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>

namespace xlsone {
namespace obf {
namespace detail {

// Compile-time obfuscation key.  Changing this value requires recompiling
// everything that uses the obfuscation macros; it does not need to be secret,
// only non-obvious in a static binary scan.  The real protection comes from
// never storing plaintext strings/bytes in .rodata when obfuscation is on.
constexpr std::array<uint8_t, 16> kObfKey = {
    0x7a, 0x19, 0x4c, 0x83, 0xe2, 0x5f, 0xa1, 0x66,
    0xb4, 0x0d, 0x71, 0x38, 0xcc, 0x92, 0x2a, 0x57};

constexpr uint8_t mixIndex(std::size_t i)
{
    // A simple but non-linear per-byte tweak to avoid all bytes using the
    // same XOR mask.  The expression is arbitrary; change with kObfKey if
    // regenerating the obfuscated secrets.
    return static_cast<uint8_t>((i * 173u + 251u) & 0xFFu);
}

// ---------- String obfuscation ----------

template <std::size_t N>
struct EncryptedString {
    std::array<uint8_t, N> data{};
    std::size_t len = 0;
};

template <std::size_t N>
consteval EncryptedString<N> encryptString(const char (&str)[N])
{
    EncryptedString<N> out;
    out.len = N - 1;
    for (std::size_t i = 0; i < N - 1; ++i) {
        out.data[i] = static_cast<uint8_t>(str[i])
            ^ kObfKey[i % kObfKey.size()]
            ^ mixIndex(i);
    }
    return out;
}

inline QString decryptString(const uint8_t* data, std::size_t len)
{
    QString result;
    result.reserve(static_cast<int>(len));
    for (std::size_t i = 0; i < len; ++i) {
        result.append(QChar(static_cast<char16_t>(
            data[i]
            ^ kObfKey[i % kObfKey.size()]
            ^ mixIndex(i))));
    }
    return result;
}

// ---------- Byte-array obfuscation ----------

template <std::size_t N>
consteval std::array<uint8_t, N> encryptByteArray(const uint8_t (&bytes)[N])
{
    std::array<uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = bytes[i] ^ kObfKey[i % kObfKey.size()] ^ mixIndex(i);
    }
    return out;
}

template <std::size_t N>
inline std::array<uint8_t, N> decryptByteArray(const std::array<uint8_t, N>& enc)
{
    std::array<uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = enc[i] ^ kObfKey[i % kObfKey.size()] ^ mixIndex(i);
    }
    return out;
}

} // namespace detail
} // namespace obf
} // namespace xlsone

// When XLSONE_OBFUSCATE is defined, sensitive literals are encrypted at
// compile time and decrypted on first use.  When it is undefined the macros
// are transparent so debugging and non-release builds are unaffected.
#ifdef XLSONE_OBFUSCATE

// Declarations for the secrets generated into obfuscated_secrets.cpp.
namespace xlsone {
namespace obf {
std::array<uint8_t, 32> licensePublicKey();
QString activationBaseUrl();
QString updateBaseUrl();
} // namespace obf
} // namespace xlsone

#define XLSONE_OBF_STRING(str) \
    ([]() -> QString { \
        constexpr auto _xlsone_enc = ::xlsone::obf::detail::encryptString(str); \
        return ::xlsone::obf::detail::decryptString( \
            _xlsone_enc.data.data(), _xlsone_enc.len); \
    }())

#define XLSONE_OBF_BYTE_ARRAY(name, ...) \
    inline auto name() \
    { \
        static constexpr uint8_t _xlsone_plain[] = {__VA_ARGS__}; \
        static constexpr auto _xlsone_enc = \
            ::xlsone::obf::detail::encryptByteArray(_xlsone_plain); \
        return ::xlsone::obf::detail::decryptByteArray(_xlsone_enc); \
    }

#else // !XLSONE_OBFUSCATE

#define XLSONE_OBF_STRING(str) QStringLiteral(str)

#define XLSONE_OBF_BYTE_ARRAY(name, ...) \
    inline auto name() \
    { \
        static constexpr uint8_t _xlsone_plain[] = {__VA_ARGS__}; \
        std::array<uint8_t, sizeof(_xlsone_plain) / sizeof(_xlsone_plain[0])> _xlsone_out; \
        for (std::size_t _xlsone_i = 0; \
             _xlsone_i < sizeof(_xlsone_plain) / sizeof(_xlsone_plain[0]); \
             ++_xlsone_i) { \
            _xlsone_out[_xlsone_i] = _xlsone_plain[_xlsone_i]; \
        } \
        return _xlsone_out; \
    }

#endif // XLSONE_OBFUSCATE
