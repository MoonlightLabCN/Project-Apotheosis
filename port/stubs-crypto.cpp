/*
 * stubs-crypto.cpp — platform WebCrypto stubs for the Win10Mobile (ARM32 thumbv7 UWP)
 * WebCore render DLL.
 *
 * 0.2.0 起 WebCore 侧的 WebCrypto 后端是上游 crypto/openssl/*.cpp(经 PlatformWinUWP.cmake
 * 的 include(platform/OpenSSL.cmake) 编进 WebCore.lib),本文件只剩 PAL::CryptoDigest ——
 * 那是 PAL 的符号,PAL 这个 port 没配 crypto 后端,所以仍由这里提供(用 OpenSSL EVP 算真 SHA)。
 * 文件下半部分原本那一整段 WebCore 假实现已删除,见文件末尾的说明。
 *
 * Policy:
 *   - PAL::CryptoDigest (create/addBytes/computeHash/dtor) may be exercised by non-WebCrypto
 *     hashing paths, so it gets a runnable no-op/zero implementation instead of an assert:
 *     computeHash() returns a fixed-length all-zero digest sized by the requested algorithm.
 *   - defaultWebCryptoMasterKey() / platformRegisterAlgorithms() / 各 platform* 现在都由
 *     上游 OpenSSL 后端提供,不再在这里出现。
 */

#include "config.h"

#include <pal/crypto/CryptoDigest.h>

#include <wtf/Assertions.h>
#include <wtf/Vector.h>

// =====================================================================================
// PAL::CryptoDigest — 真实 SHA(走已为 TLS 链接的 OpenSSL libcrypto)。
// Apotheosis: 原先 computeHash() 返回全 0 假摘要 → Subresource Integrity(integrity=...)
// 等校验恒失败、资源被拦。改用 OpenSSL EVP 一次性摘要算真 SHA。
// 只声明所需 C 函数(不引 openssl 头,避免与 WebKit 自带 crypto 头/config 冲突)。
// =====================================================================================

#include <openssl/evp.h>   // libcrypto(已为 TLS 链接)的 EVP 摘要;头已在 vcpkg include 路径上

namespace PAL {

// 累积 addBytes 的输入,computeHash() 时一次性算摘要。
struct CryptoDigestContext {
    CryptoDigestHashFunction algorithm { CryptoDigestHashFunction::SHA_256 };
    Vector<uint8_t> data;
};

CryptoDigest::CryptoDigest()
    : m_context(nullptr)
{
}

CryptoDigest::~CryptoDigest() = default;

std::unique_ptr<CryptoDigest> CryptoDigest::create(CryptoDigestHashFunction algorithm)
{
    auto digest = std::unique_ptr<CryptoDigest>(new CryptoDigest);
    digest->m_context = std::unique_ptr<CryptoDigestContext>(new CryptoDigestContext { algorithm });
    return digest;
}

void CryptoDigest::addBytes(std::span<const uint8_t> bytes)
{
    if (m_context)
        m_context->data.append(bytes);
}

Vector<uint8_t> CryptoDigest::computeHash()
{
    const EVP_MD* md = EVP_sha256();
    if (m_context) {
        switch (m_context->algorithm) {
        case CryptoDigestHashFunction::SHA_1:              md = EVP_sha1();   break;
        case CryptoDigestHashFunction::DEPRECATED_SHA_224: md = EVP_sha224(); break;
        case CryptoDigestHashFunction::SHA_256:            md = EVP_sha256(); break;
        case CryptoDigestHashFunction::SHA_384:            md = EVP_sha384(); break;
        case CryptoDigestHashFunction::SHA_512:            md = EVP_sha512(); break;
        }
    }
    auto in = m_context ? m_context->data.span() : std::span<const uint8_t>();
    unsigned char hash[64]; // EVP_MAX_MD_SIZE
    unsigned int len = 0;
    Vector<uint8_t> result;
    if (EVP_Digest(in.data(), in.size(), hash, &len, md, nullptr) == 1)
        result.append(std::span<const uint8_t>(hash, len));
    return result;
}

} // namespace PAL

// =====================================================================================
// WebCore WebCrypto backend — 0.2.0 起由上游 OpenSSL 后端提供,本文件不再出假实现。
//
// 0.1.9 及更早,这里有一整段 `namespace WebCore { ... }`:CryptoAlgorithmRegistry::
// platformRegisterAlgorithms() 是空函数(注册表恒空 → crypto.subtle 的每个算法都
// NotSupportedError),CryptoKeyEC/CryptoKeyRSA/CryptoAlgorithm* 的 platform* 全是
// RELEASE_ASSERT_NOT_REACHED()。
//
// 0.2.0 在 PlatformWinUWP.cmake 里 include(platform/OpenSSL.cmake),把上游那 18 个
// crypto/openssl/*.cpp 编进 WebCore.lib,以上符号全部由上游真实现提供 —— 这段假实现
// 必须整体删掉,否则与 WebCore.lib 里的定义 LNK2005 重复。
//
// 仍然留在本文件里的是 PAL::CryptoDigest(见上方):它属于 PAL,不在 OpenSSL.cmake 的
// 覆盖范围内,而 PAL 这个 port 没有配 crypto 后端。
// =====================================================================================
