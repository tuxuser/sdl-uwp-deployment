// Incremental SHA-256 through Windows CNG (BCrypt), which store apps may use.
#pragma once

#include <cstddef>

#include "manifest/manifest.h"

namespace onboarding {

class Sha256 {
 public:
  Sha256();
  ~Sha256();
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;

  void Update(const void* data, size_t size);
  // Ends the hash. The object cannot be reused.
  Sha256Digest Finish();

 private:
  void* algorithm_ = nullptr;
  void* hash_ = nullptr;
};

}  // namespace onboarding
