//===--- SILType.h - Defines the SILType type -------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the SILType class, which is used to refer to SIL
// representation types.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_SILTYPE_H
#define SWIFT_SIL_SILTYPE_H

#include "swift/AST/CanTypeVisitor.h"
#include "swift/AST/Types.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/ErrorHandling.h"
#include "swift/SIL/SILAllocated.h"
#include "swift/SIL/SILArgumentConvention.h"
#include "llvm/ADT/Hashing.h"
#include "swift/SIL/SILDeclRef.h"

namespace swift {

class ASTContext;
class VarDecl;
class SILFunction;

namespace Lowering {
  class AbstractionPattern;
  class TypeConverter;
}

} // end namespace swift
  
namespace swift {

/// How an existential type container is represented.
enum class ExistentialRepresentation {
  /// The type is not existential.
  None,
  /// The container uses an opaque existential container, with a fixed-sized
  /// buffer. The type is address-only and is manipulated using the
  /// {init,open,deinit}_existential_addr family of instructions.
  Opaque,
  /// The container uses a class existential container, which holds a reference
  /// to the class instance that conforms to the protocol. The type is
  /// reference-counted and is manipulated using the
  /// {init,open}_existential_ref family of instructions.
  Class,
  /// The container uses a metatype existential container, which holds a
  /// reference to the type metadata for a type that
  /// conforms to the protocol. The type is trivial, and is manipulated using
  /// the {init,open}_existential_metatype family of instructions.
  Metatype,
  /// The container uses a boxed existential container, which is a
  /// reference-counted buffer that indirectly contains the
  /// conforming value. The type is manipulated
  /// using the {alloc,open,dealloc}_existential_box family of instructions.
  /// The container may be able to directly adopt a class reference using
  /// init_existential_ref for some class types.
  Boxed,
};

/// The value category.
enum class SILValueCategory : uint8_t {
  /// An object is a value of the type.
  Object,

  /// An address is a pointer to an allocated variable of the type
  /// (possibly uninitialized).
  Address,
};

/// SILType - A Swift type that has been lowered to a SIL representation type.
/// In addition to the Swift type system, SIL adds "address" types that can
/// reference any Swift type (but cannot take the address of an address). *T
/// is the type of an address pointing at T.
///
class SILType {
public:
  /// The unsigned is a SILValueCategory.
  using ValueType = llvm::PointerIntPair<TypeBase *, 2, unsigned>;
private:
  ValueType value;

  /// Private constructor. SILTypes are normally vended by
  /// TypeConverter::getLoweredType().
  SILType(CanType ty, SILValueCategory category)
      : value(ty.getPointer(), unsigned(category)) {
    if (!ty) return;
    assert(ty->isLegalSILType() &&
           "constructing SILType with type that should have been "
           "eliminated by SIL lowering");
  }
  
  SILType(ValueType value) : value(value) {
  }

  friend class Lowering::TypeConverter;
  friend struct llvm::DenseMapInfo<SILType>;
public:
  SILType() = default;

  /// getPrimitiveType - Form a SILType for a primitive type that does not
  /// require any special handling (i.e., not a function or aggregate type).
  static SILType getPrimitiveType(CanType T, SILValueCategory category) {
    return SILType(T, category);
  }

  /// Form the type of an r-value, given a Swift type that either does
  /// not require any special handling or has already been
  /// appropriately lowered.
  static SILType getPrimitiveObjectType(CanType T) {
    return SILType(T, SILValueCategory::Object);
  }

  /// Form the type for the address of an object, given a Swift type
  /// that either does not require any special handling or has already
  /// been appropriately lowered.
  static SILType getPrimitiveAddressType(CanType T) {
    return SILType(T, SILValueCategory::Address);
  }

  bool isNull() const { return value.getPointer() == nullptr; }
  explicit operator bool() const { return bool(value.getPointer()); }

  SILValueCategory getCategory() const {
    return SILValueCategory(value.getInt());
  }

  /// Returns the \p Category variant of this type.
  SILType getCategoryType(SILValueCategory Category) const {
    return SILType(getASTType(), Category);
  }

  /// Returns the variant of this type that matches \p Ty.getCategory()
  SILType copyCategory(SILType Ty) const {
    return getCategoryType(Ty.getCategory());
  }

  /// Returns the address variant of this type.  Instructions which
  /// manipulate memory will generally work with object addresses.
  SILType getAddressType() const {
    return SILType(getASTType(), SILValueCategory::Address);
  }

  /// Returns the object variant of this type.  Note that address-only
  /// types are not legal to manipulate directly as objects in SIL.
  SILType getObjectType() const {
    return SILType(getASTType(), SILValueCategory::Object);
  }

  /// Returns the canonical AST type referenced by this SIL type.
  ///
  /// NOTE:
  /// 1. The returned AST type may not be a proper formal type.
  ///    For example, it may contain a SILFunctionType instead of a
  ///    FunctionType.
  /// 2. The returned type may not be the same as the original
  ///    unlowered type that produced this SILType (even after
  ///    canonicalization). If you need it, you must pass it separately.
  ///    For example, `AnyObject.Type` may get lowered to
  ///    `$@thick AnyObject.Type`, for which the AST type will be
  ///    `@thick AnyObject.Type`.
  ///    More generally, you cannot recover a formal type from
  ///    a lowered type. See docs/SIL.rst for more details.
  CanType getASTType() const {
    return CanType(value.getPointer());
  }
  
  // FIXME -- Temporary until LLDB adopts getASTType()
  LLVM_ATTRIBUTE_DEPRECATED(CanType getSwiftRValueType() const,
                            "Please use getASTType()") {
    return getASTType();
  }

  /// Returns the AbstractCC of a function type.
  /// The SILType must refer to a function type.
  SILFunctionTypeRepresentation getFunctionRepresentation() const {
    return castTo<SILFunctionType>()->getRepresentation();
  }

  /// Cast the Swift type referenced by this SIL type, or return null if the
  /// cast fails.
  template<typename TYPE>
  typename CanTypeWrapperTraits<TYPE>::type
  getAs() const { return dyn_cast<TYPE>(getASTType()); }

  /// Cast the Swift type referenced by this SIL type, which must be of the
  /// specified subtype.
  template<typename TYPE>
  typename CanTypeWrapperTraits<TYPE>::type
  castTo() const { return cast<TYPE>(getASTType()); }

  /// Returns true if the Swift type referenced by this SIL type is of the
  /// specified subtype.
  template<typename TYPE>
  bool is() const { return isa<TYPE>(getASTType()); }

  bool isVoid() const {
    return value.getPointer()->isVoid();
  }

  /// Retrieve the ClassDecl for a type that maps to a Swift class or
  /// bound generic class type.
  ClassDecl *getClassOrBoundGenericClass() const {
    return getASTType().getClassOrBoundGenericClass();
  }
  /// Retrieve the StructDecl for a type that maps to a Swift struct or
  /// bound generic struct type.
  StructDecl *getStructOrBoundGenericStruct() const {
    return getASTType().getStructOrBoundGenericStruct();
  }
  /// Retrieve the EnumDecl for a type that maps to a Swift enum or
  /// bound generic enum type.
  EnumDecl *getEnumOrBoundGenericEnum() const {
    return getASTType().getEnumOrBoundGenericEnum();
  }
  /// Retrieve the NominalTypeDecl for a type that maps to a Swift
  /// nominal or bound generic nominal type.
  NominalTypeDecl *getNominalOrBoundGenericNominal() const {
    return getASTType().getNominalOrBoundGenericNominal();
  }
  
  /// True if the type is an address type.
  bool isAddress() const { return getCategory() == SILValueCategory::Address; }

  /// True if the type is an object type.
  bool isObject() const { return getCategory() == SILValueCategory::Object; }

  /// isAddressOnly - True if the type, or the referenced type of an address
  /// type, is address-only.  For example, it could be a resilient struct or
  /// something of unknown size.
  ///
  /// This is equivalent to, but possibly faster than, calling
  /// tc.getTypeLowering(type).isAddressOnly().
  static bool isAddressOnly(CanType type, Lowering::TypeConverter &tc,
                            CanGenericSignature sig,
                            ResilienceExpansion expansion);

  /// Return true if this type must be returned indirectly.
  ///
  /// This is equivalent to, but possibly faster than, calling
  /// tc.getTypeLowering(type).isReturnedIndirectly().
  static bool isFormallyReturnedIndirectly(CanType type,
                                           Lowering::TypeConverter &tc,
                                           CanGenericSignature sig) {
    return isAddressOnly(type, tc, sig, ResilienceExpansion::Minimal);
  }

  /// Return true if this type must be passed indirectly.
  ///
  /// This is equivalent to, but possibly faster than, calling
  /// tc.getTypeLowering(type).isPassedIndirectly().
  static bool isFormallyPassedIndirectly(CanType type,
                                         Lowering::TypeConverter &tc,
                                         CanGenericSignature sig) {
    return isAddressOnly(type, tc, sig, ResilienceExpansion::Minimal);
  }

  /// True if the type, or the referenced type of an address type, is loadable.
  /// This is the opposite of isAddressOnly.
  bool isLoadable(const SILFunction &F) const {
    return !isAddressOnly(F);
  }

  /// True if either:
  /// 1) The type, or the referenced type of an address type, is loadable.
  /// 2) The SIL Module conventions uses lowered addresses
  bool isLoadableOrOpaque(const SILFunction &F) const;

  /// True if the type, or the referenced type of an address type, is
  /// address-only. This is the opposite of isLoadable.
  bool isAddressOnly(const SILFunction &F) const;

  /// True if the underlying AST type is trivial, meaning it is loadable and can
  /// be trivially copied, moved or detroyed. Returns false for address types
  /// even though they are technically trivial.
  bool isTrivial(const SILFunction &F) const;

  /// True if the type, or the referenced type of an address type, is known to
  /// be a scalar reference-counted type such as a class, box, or thick function
  /// type. Returns false for non-trivial aggregates.
  bool isReferenceCounted(SILModule &M) const;

  /// Returns true if the referenced type is a function type that never
  /// returns.
  bool isNoReturnFunction(SILModule &M) const;

  /// Returns true if the referenced AST type has reference semantics, even if
  /// the lowered SIL type is known to be trivial.
  bool hasReferenceSemantics() const {
    return getASTType().hasReferenceSemantics();
  }

  /// Returns true if the referenced type is any sort of class-reference type,
  /// meaning anything with reference semantics that is not a function type.
  bool isAnyClassReferenceType() const {
    return getASTType().isAnyClassReferenceType();
  }
  
  /// Returns true if the referenced type is guaranteed to have a
  /// single-retainable-pointer representation.
  bool hasRetainablePointerRepresentation() const {
    return getASTType()->hasRetainablePointerRepresentation();
  }
  /// Returns true if the referenced type is an existential type.
  bool isExistentialType() const {
    return getASTType().isExistentialType();
  }
  /// Returns true if the referenced type is any kind of existential type.
  bool isAnyExistentialType() const {
    return getASTType().isAnyExistentialType();
  }
  /// Returns true if the referenced type is a class existential type.
  bool isClassExistentialType() const {
    return getASTType()->isClassExistentialType();
  }

  /// Returns true if the referenced type is an opened existential type
  /// (which is actually a kind of archetype).
  bool isOpenedExistential() const {
    return getASTType()->isOpenedExistential();
  }

  /// Returns true if the referenced type is expressed in terms of one
  /// or more opened existential types.
  bool hasOpenedExistential() const {
    return getASTType()->hasOpenedExistential();
  }
  
  /// Returns the representation used by an existential type. If the concrete
  /// type is provided, this may return a specialized representation kind that
  /// can be used for that type. Otherwise, returns the most general
  /// representation kind for the type. Returns None if the type is not an
  /// existential type.
  ExistentialRepresentation
  getPreferredExistentialRepresentation(Type containedType = Type()) const;
  
  /// Returns true if the existential type can use operations for the given
  /// existential representation when working with values of the given type,
  /// or when working with an unknown type if containedType is null.
  bool
  canUseExistentialRepresentation(ExistentialRepresentation repr,
                                  Type containedType = Type()) const;
  
  /// True if the type contains a type parameter.
  bool hasTypeParameter() const {
    return getASTType()->hasTypeParameter();
  }
  
  /// True if the type is bridgeable to an ObjC object pointer type.
  bool isBridgeableObjectType() const {
    return getASTType()->isBridgeableObjectType();
  }

  static bool isClassOrClassMetatype(Type t) {
    if (auto *meta = t->getAs<AnyMetatypeType>()) {
      return bool(meta->getInstanceType()->getClassOrBoundGenericClass());
    } else {
      return bool(t->getClassOrBoundGenericClass());
    }
  }

  /// True if the type is a class type or class metatype type.
  bool isClassOrClassMetatype() {
    return isObject() && isClassOrClassMetatype(getASTType());
  }

  /// True if the type involves any archetypes.
  bool hasArchetype() const {
    return getASTType()->hasArchetype();
  }
  
  /// Returns the ASTContext for the referenced Swift type.
  ASTContext &getASTContext() const {
    return getASTType()->getASTContext();
  }

  /// True if the given type has at least the size and alignment of a native
  /// pointer.
  bool isPointerSizeAndAligned();

  /// True if `operTy` can be cast by single-reference value into `resultTy`.
  static bool canRefCast(SILType operTy, SILType resultTy, SILModule &M);

  /// True if the type is block-pointer-compatible, meaning it either is a block
  /// or is an Optional with a block payload.
  bool isBlockPointerCompatible() const {
    // Look through one level of optionality.
    SILType ty = *this;
    if (auto optPayload = ty.getOptionalObjectType()) {
      ty = optPayload;
    }
      
    auto fTy = ty.getAs<SILFunctionType>();
    if (!fTy)
      return false;
    return fTy->getRepresentation() == SILFunctionType::Representation::Block;
  }

  /// Given that this is a nominal type, return the lowered type of
  /// the given field.  Applies substitutions as necessary.  The
  /// result will be an address type if the base type is an address
  /// type or a class.
  SILType getFieldType(VarDecl *field, Lowering::TypeConverter &TC) const;

  SILType getFieldType(VarDecl *field, SILModule &M) const;

  /// Given that this is an enum type, return the lowered type of the
  /// data for the given element.  Applies substitutions as necessary.
  /// The result will have the same value category as the base type.
  SILType getEnumElementType(EnumElementDecl *elt, Lowering::TypeConverter &TC) const;

  SILType getEnumElementType(EnumElementDecl *elt, SILModule &M) const;

  /// Given that this is a tuple type, return the lowered type of the
  /// given tuple element.  The result will have the same value
  /// category as the base type.
  SILType getTupleElementType(unsigned index) const {
    return SILType(castTo<TupleType>().getElementType(index), getCategory());
  }

  /// Return the immediate superclass type of this type, or null if
  /// it's the most-derived type.
  SILType getSuperclass() const {
    auto superclass = getASTType()->getSuperclass();
    if (!superclass) return SILType();
    return SILType::getPrimitiveObjectType(superclass->getCanonicalType());
  }

  /// Return true if Ty is a subtype of this exact SILType, or false otherwise.
  bool isExactSuperclassOf(SILType Ty) const {
    return getASTType()->isExactSuperclassOf(Ty.getASTType());
  }

  /// Return true if Ty is a subtype of this SILType, or if this SILType
  /// contains archetypes that can be found to form a supertype of Ty, or false
  /// otherwise.
  bool isBindableToSuperclassOf(SILType Ty) const {
    return getASTType()->isBindableToSuperclassOf(Ty.getASTType());
  }

  /// Look through reference-storage types on this type.
  SILType getReferenceStorageReferentType() const {
    return SILType(getASTType().getReferenceStorageReferent(), getCategory());
  }

  /// Transform the function type SILType by replacing all of its interface
  /// generic args with the appropriate item from the substitution.
  ///
  /// Only call this with function types!
  SILType substGenericArgs(Lowering::TypeConverter &TC,
                           SubstitutionMap SubMap) const;

  SILType substGenericArgs(SILModule &M,
                           SubstitutionMap SubMap) const;

  /// If the original type is generic, pass the signature as genericSig.
  ///
  /// If the replacement types are generic, you must push a generic context
  /// first.
  SILType subst(Lowering::TypeConverter &tc, TypeSubstitutionFn subs,
                LookupConformanceFn conformances,
                CanGenericSignature genericSig = CanGenericSignature(),
                bool shouldSubstituteOpaqueArchetypes = false) const;

  SILType subst(SILModule &M, TypeSubstitutionFn subs,
                LookupConformanceFn conformances,
                CanGenericSignature genericSig = CanGenericSignature(),
                bool shouldSubstituteOpaqueArchetypes = false) const;

  SILType subst(Lowering::TypeConverter &tc, SubstitutionMap subs) const;

  SILType subst(SILModule &M, SubstitutionMap subs) const;

  /// Return true if this type references a "ref" type that has a single pointer
  /// representation. Class existentials do not always qualify.
  bool isHeapObjectReferenceType() const;

  /// Returns true if this SILType is an aggregate that contains \p Ty
  bool aggregateContainsRecord(SILType Ty, SILModule &SILMod) const;
  
  /// Returns true if this SILType is an aggregate with unreferenceable storage,
  /// meaning it cannot be fully destructured in SIL.
  bool aggregateHasUnreferenceableStorage() const;

  /// Returns the lowered type for T if this type is Optional<T>;
  /// otherwise, return the null type.
  SILType getOptionalObjectType() const;

  /// Unwraps one level of optional type.
  /// Returns the lowered T if the given type is Optional<T>.
  /// Otherwise directly returns the given type.
  SILType unwrapOptionalType() const;

  /// Returns true if this is the AnyObject SILType;
  bool isAnyObject() const { return getASTType()->isAnyObject(); }
  
  /// Returns a SILType with any archetypes mapped out of context.
  SILType mapTypeOutOfContext() const;

  /// Given two SIL types which are representations of the same type,
  /// check whether they have an abstraction difference.
  bool hasAbstractionDifference(SILFunctionTypeRepresentation rep,
                                SILType type2);

  /// Returns true if this SILType could be potentially a lowering of the given
  /// formal type. Meant for verification purposes/assertions.
  bool isLoweringOf(SILModule &M, CanType formalType);

  /// Returns the hash code for the SILType.
  llvm::hash_code getHashCode() const {
    return llvm::hash_combine(*this);
  }

  //
  // Accessors for types used in SIL instructions:
  //
  
  /// Get the NativeObject type as a SILType.
  static SILType getNativeObjectType(const ASTContext &C);
  /// Get the BridgeObject type as a SILType.
  static SILType getBridgeObjectType(const ASTContext &C);
  /// Get the RawPointer type as a SILType.
  static SILType getRawPointerType(const ASTContext &C);
  /// Get a builtin integer type as a SILType.
  static SILType getBuiltinIntegerType(unsigned bitWidth, const ASTContext &C);
  /// Get the IntegerLiteral type as a SILType.
  static SILType getBuiltinIntegerLiteralType(const ASTContext &C);
  /// Get a builtin floating-point type as a SILType.
  static SILType getBuiltinFloatType(BuiltinFloatType::FPKind Kind,
                                     const ASTContext &C);
  /// Get the builtin word type as a SILType;
  static SILType getBuiltinWordType(const ASTContext &C);

  /// Given a value type, return an optional type wrapping it.
  static SILType getOptionalType(SILType valueType);

  /// Get the standard exception type.
  static SILType getExceptionType(const ASTContext &C);

  /// Get the SIL token type.
  static SILType getSILTokenType(const ASTContext &C);

  //
  // Utilities for treating SILType as a pointer-like type.
  //
  static SILType getFromOpaqueValue(void *P) {
    return SILType(ValueType::getFromOpaqueValue(P));
  }
  void *getOpaqueValue() const {
    return value.getOpaqueValue();
  }
  
  bool operator==(SILType rhs) const {
    return value.getOpaqueValue() == rhs.value.getOpaqueValue();
  }
  bool operator!=(SILType rhs) const {
    return value.getOpaqueValue() != rhs.value.getOpaqueValue();
  }

  /// Return the mangled name of this type, ignoring its prefix. Meant for
  /// diagnostic purposes.
  std::string getMangledName() const;

  std::string getAsString() const;
  void dump() const;
  void print(raw_ostream &OS) const;
};

// Statically prevent SILTypes from being directly cast to a type
// that's not legal as a SIL value.
#define NON_SIL_TYPE(ID)                                             \
template<> Can##ID##Type SILType::getAs<ID##Type>() const = delete;  \
template<> Can##ID##Type SILType::castTo<ID##Type>() const = delete; \
template<> bool SILType::is<ID##Type>() const = delete;
NON_SIL_TYPE(Function)
NON_SIL_TYPE(AnyFunction)
NON_SIL_TYPE(LValue)
#undef NON_SIL_TYPE

CanSILFunctionType getNativeSILFunctionType(
    Lowering::TypeConverter &TC, Lowering::AbstractionPattern origType,
    CanAnyFunctionType substType, Optional<SILDeclRef> origConstant = None,
    Optional<SILDeclRef> constant = None,
    Optional<SubstitutionMap> reqtSubs = None,
    ProtocolConformanceRef witnessMethodConformance = ProtocolConformanceRef());

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, SILType T) {
  T.print(OS);
  return OS;
}

inline SILType SILBlockStorageType::getCaptureAddressType() const {
  return SILType::getPrimitiveAddressType(getCaptureType());
}

/// The hash of a SILType is the hash of its opaque value.
static inline llvm::hash_code hash_value(SILType V) {
  return llvm::hash_value(V.getOpaqueValue());
}

inline SILType SILField::getAddressType() const {
  return SILType::getPrimitiveAddressType(getLoweredType());
}
inline SILType SILField::getObjectType() const {
  return SILType::getPrimitiveObjectType(getLoweredType());
}

CanType getSILBoxFieldLoweredType(SILBoxType *type,
                                  Lowering::TypeConverter &TC,
                                  unsigned index);

inline SILType getSILBoxFieldType(SILBoxType *type,
                                  Lowering::TypeConverter &TC,
                                  unsigned index) {
  return SILType::getPrimitiveAddressType(
    getSILBoxFieldLoweredType(type, TC, index));
}

} // end swift namespace

namespace llvm {

// Allow the low bit of SILType to be used for nefarious purposes, e.g. putting
// a SILType into a PointerUnion.
template<>
struct PointerLikeTypeTraits<swift::SILType> {
public:
  static inline void *getAsVoidPointer(swift::SILType T) {
    return T.getOpaqueValue();
  }
  static inline swift::SILType getFromVoidPointer(void *P) {
    return swift::SILType::getFromOpaqueValue(P);
  }
  // SILType is just a wrapper around its ValueType, so it has a bit available.
  enum { NumLowBitsAvailable =
    PointerLikeTypeTraits<swift::SILType::ValueType>::NumLowBitsAvailable };
};


// Allow SILType to be used as a DenseMap key.
template<>
struct DenseMapInfo<swift::SILType> {
  using SILType = swift::SILType;
  using PointerMapInfo = DenseMapInfo<void*>;
public:
  static SILType getEmptyKey() {
    return SILType::getFromOpaqueValue(PointerMapInfo::getEmptyKey());
  }
  static SILType getTombstoneKey() {
    return SILType::getFromOpaqueValue(PointerMapInfo::getTombstoneKey());
  }
  static unsigned getHashValue(SILType t) {
    return PointerMapInfo::getHashValue(t.getOpaqueValue());
  }
  static bool isEqual(SILType LHS, SILType RHS) {
    return LHS == RHS;
  }
};

} // end llvm namespace

#endif
