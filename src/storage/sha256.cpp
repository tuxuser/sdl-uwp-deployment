#include "sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>

namespace onboarding {
namespace {

void Check(NTSTATUS status, const char* what) {
  if (!BCRYPT_SUCCESS(status)) throw std::runtime_error(what);
}

}  // namespace

Sha256::Sha256() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
        "BCryptOpenAlgorithmProvider");
  algorithm_ = algorithm;
  BCRYPT_HASH_HANDLE hash = nullptr;
  Check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0), "BCryptCreateHash");
  hash_ = hash;
}

Sha256::~Sha256() {
  if (hash_) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
  if (algorithm_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

void Sha256::Update(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (size > 0) {
    ULONG chunk = static_cast<ULONG>(size > 0x40000000 ? 0x40000000 : size);
    Check(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_), const_cast<PUCHAR>(bytes), chunk,
                         0),
          "BCryptHashData");
    bytes += chunk;
    size -= chunk;
  }
}

Sha256Digest Sha256::Finish() {
  Sha256Digest digest{};
  Check(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), digest.data(),
                         static_cast<ULONG>(digest.size()), 0),
        "BCryptFinishHash");
  return digest;
}

}  // namespace onboarding
