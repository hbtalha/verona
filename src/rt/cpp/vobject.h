// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../region/region.h"

#include <type_traits>

namespace verona::rt
{
  using namespace snmalloc;

  // These helpers are used to determine if various methods are provided by the
  // child class of V<>. They intentionally only check for the name of the
  // method, not for its precise signature.
  //
  // If for example a class C has a notified method with an incorrect
  // signature, `has_notified<C>` will still be true. However the
  // implementation of V<C> (in this case `gc_notified`) would not compile.
  //
  // This is better than ignoring methods with the right name but the wrong
  // signature.
  template<class T, class = void>
  struct has_notified : std::false_type
  {};
  template<class T>
  struct has_notified<T, std::void_t<decltype(&T::notified)>> : std::true_type
  {};

  template<class T, class = void>
  struct has_finaliser : std::false_type
  {};
  template<class T>
  struct has_finaliser<T, std::void_t<decltype(&T::finaliser)>> : std::true_type
  {};

  template<class T>
  struct has_destructor
  {
    constexpr static bool value = !std::is_trivially_destructible_v<T>;
  };

  template<
    class T,
    RegionType region_type = RegionType::Trace,
    class Base = Object>
  class V : public Base
  {
  private:
    static_assert(
      std::is_same_v<Base, Object> ?
        region_type == RegionType::Trace || region_type == RegionType::Arena :
        region_type == RegionType::Cown);
    static_assert(
      std::is_same_v<Base, Object> || std::is_same_v<Base, Cown>,
      "V base must be Object or Cown");

    using RegionClass = typename RegionType_to_class<region_type>::T;

    static void gc_trace(const Object* o, ObjectStack& st)
    {
      ((T*)o)->trace(st);
    }

    static void gc_notified(Object* o)
    {
      if constexpr (has_notified<T>::value)
        ((T*)o)->notified(o);
    }

    static void gc_final(Object* o, Object* region, ObjectStack& sub_regions)
    {
      if constexpr (has_finaliser<T>::value)
        ((T*)o)->finaliser(region, sub_regions);
    }

    static void gc_destructor(Object* o)
    {
      ((T*)o)->~T();
    }

    static constexpr Descriptor desc = {
      vsizeof<T>,
      gc_trace,
      has_finaliser<T>::value ? gc_final : nullptr,
      has_notified<T>::value ? gc_notified : nullptr,
      has_destructor<T>::value ? gc_destructor : nullptr};

    void trace(ObjectStack&) {}

    static EpochMark get_alloc_epoch()
    {
      return Scheduler::alloc_epoch();
    }

  public:
    V() : Base() {}

    void* operator new(size_t)
    {
      if constexpr (std::is_same_v<Base, Object>)
        return RegionClass::template create<desc.size>(
          ThreadAlloc::get(), &desc);
      else
        return Object::register_object(
          ThreadAlloc::get()->alloc<desc.size>(), &desc);
    }

    void* operator new(size_t, Alloc* alloc)
    {
      if constexpr (std::is_same_v<Base, Object>)
        return RegionClass::template create<desc.size>(alloc, &desc);
      else
        return Object::register_object(alloc->alloc<desc.size>(), &desc);
    }

    void* operator new(size_t, Object* region)
    {
      static_assert(std::is_same_v<Base, Object>, "Don't call on cowns.");
      return RegionClass::template alloc<desc.size>(
        ThreadAlloc::get(), region, &desc);
    }

    void* operator new(size_t, Alloc* alloc, Object* region)
    {
      static_assert(std::is_same_v<Base, Object>, "Don't call on cowns.");
      if constexpr (std::is_same_v<Base, Object>)
        return RegionClass::template alloc<desc.size>(alloc, region, &desc);
    }

    void operator delete(void*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, size_t)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Alloc*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Object*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Alloc*, Object*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void* operator new[](size_t size) = delete;
    void operator delete[](void* p) = delete;
    void operator delete[](void* p, size_t sz) = delete;
  };

  // Cowns are not allocated inside regions, but we still need a RegionType.
  template<class T>
  using VCown = V<T, RegionType::Cown, Cown>;
} // namespace verona::rt
