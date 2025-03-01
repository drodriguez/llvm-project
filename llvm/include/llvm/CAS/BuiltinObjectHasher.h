//===- BuiltinObjectHasher.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_BUILTINOBJECTHASHER_H
#define LLVM_CAS_BUILTINOBJECTHASHER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/CAS/TreeEntry.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace cas {

template <class HasherT> class BuiltinObjectHasher {
public:
  using HashT = decltype(HasherT::hash(std::declval<ArrayRef<uint8_t> &>()));

  static HashT hashObject(const ObjectStore &CAS, ArrayRef<ObjectRef> Refs,
                          ArrayRef<char> Data) {
    BuiltinObjectHasher H;
    H.updateSize(Refs.size());
    for (const ObjectRef &Ref : Refs)
      H.updateRef(CAS, Ref);
    H.updateArray(Data);
    return H.finish();
  }

private:
  HashT finish() { return Hasher.final(); }

  void updateRef(const ObjectStore &CAS, ObjectRef Ref) {
    updateID(CAS.getID(Ref));
  }

  void updateID(const CASID &ID) {
    // NOTE: Does not hash the size of the hash. That's a CAS implementation
    // detail that shouldn't leak into the UUID for an object.
    ArrayRef<uint8_t> Hash = ID.getHash();
    assert(Hash.size() == sizeof(HashT) &&
           "Expected object ref to match the hash size");
    Hasher.update(Hash);
  }

  void updateArray(ArrayRef<uint8_t> Bytes) {
    updateSize(Bytes.size());
    Hasher.update(Bytes);
  }

  void updateArray(ArrayRef<char> Bytes) {
    updateArray(makeArrayRef(reinterpret_cast<const uint8_t *>(Bytes.data()),
                             Bytes.size()));
  }

  void updateSize(uint64_t Size) {
    Size = support::endian::byte_swap(Size, support::endianness::little);
    Hasher.update(makeArrayRef(reinterpret_cast<const uint8_t *>(&Size),
                                sizeof(Size)));
  }

  BuiltinObjectHasher() = default;
  ~BuiltinObjectHasher() = default;
  HasherT Hasher;
};

} // namespace cas
} // namespace llvm

#endif // LLVM_CAS_BUILTINOBJECTHASHER_H
