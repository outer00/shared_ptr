#pragma once
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

template <typename T>
class SharedPtr;

template <typename T>
class WeakPtr;

template <typename T>
class EnableSharedFromThis;

struct BaseControlBlock;

template <typename T, typename Deleter, typename Alloc>
struct ControlBlock;

struct BaseControlBlock {
  size_t shared;
  size_t weak;

  BaseControlBlock(size_t shared, size_t weak) : shared(shared), weak(weak) {}
  virtual ~BaseControlBlock() = default;

  virtual void destroy() = 0;
  virtual void deallocate() = 0;
};

template <typename T, typename Alloc = std::allocator<T>>
struct MakeSharedControlBlock : BaseControlBlock {
  [[no_unique_address]] Alloc alloc;

  using BlockAlloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<MakeSharedControlBlock<T, Alloc>>;
  using BlockAllocTraits = typename std::allocator_traits<
      Alloc>::template rebind_traits<MakeSharedControlBlock<T, Alloc>>;

  template <typename... Args>
  MakeSharedControlBlock(size_t shared, size_t weak, Alloc a, Args&&... args)
      : BaseControlBlock(shared, weak), alloc(a) {
    std::allocator_traits<Alloc>::construct(
        alloc, reinterpret_cast<T*>(this + 1), std::forward<Args>(args)...);
  }
  ~MakeSharedControlBlock() override = default;

  void destroy() override {
    std::allocator_traits<Alloc>::destroy(alloc,
                                          reinterpret_cast<T*>(this + 1));
  }
  void deallocate() override {
    BlockAlloc cb_alloc = alloc;
    BlockAllocTraits::deallocate(cb_alloc, this, 1);
  }

  T& get_ptr() { return *reinterpret_cast<T*>(this + 1); }
  const T& get_ptr() const { return *reinterpret_cast<const T*>(this + 1); }
};

template <typename T, typename Deleter, typename Alloc>
struct ControlBlock : BaseControlBlock {
  T* ptr;
  [[no_unique_address]] Deleter del;
  [[no_unique_address]] Alloc alloc;

  using BlockAlloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<ControlBlock<T, Deleter, Alloc>>;
  using BlockAllocTraits = typename std::allocator_traits<
      Alloc>::template rebind_traits<ControlBlock<T, Deleter, Alloc>>;

  ControlBlock(size_t shared, size_t weak, T* ptr, const Deleter& d, Alloc a)
      : BaseControlBlock(shared, weak), ptr(ptr), del(d), alloc(a) {}
  ~ControlBlock() override = default;

  void deallocate() override {
    BlockAlloc cb_alloc = alloc;
    BlockAllocTraits::deallocate(cb_alloc, this, 1);
  }
  void destroy() override { del(ptr); }
};

template <typename T>
class SharedPtr {
  T* ptr = nullptr;
  BaseControlBlock* cb = nullptr;

 public:
  SharedPtr();

  template <typename Deleter = std::default_delete<T>,
            typename Alloc = std::allocator<T>>
  SharedPtr(T* ptr, const Deleter& del = Deleter(), Alloc alloc = Alloc());

  template <typename Y, typename Deleter = std::default_delete<T>,
            typename Alloc = std::allocator<T>>
  SharedPtr(Y* ptr, const Deleter& del = Deleter(), Alloc alloc = Alloc());

  SharedPtr(const SharedPtr& shptr) noexcept;

  template <typename Y>
  SharedPtr(const SharedPtr<Y>& shptr) noexcept;

  SharedPtr(SharedPtr&& shptr) noexcept;

  template <typename Y>
  SharedPtr(SharedPtr<Y>&& shptr) noexcept;

  SharedPtr& operator=(const SharedPtr& shptr) noexcept;

  template <typename Y>
  SharedPtr& operator=(const SharedPtr<Y>& shptr) noexcept;

  SharedPtr& operator=(SharedPtr&& shptr) noexcept;

  template <typename Y>
  SharedPtr& operator=(SharedPtr<Y>&& shptr) noexcept;

  T& operator*() const noexcept;

  T* operator->() const noexcept;

  ~SharedPtr();

  size_t use_count() const;

  T* get() const;

  void reset();

  template <typename Y, typename Deleter = std::default_delete<T>,
            typename Alloc = std::allocator<T>>
  void reset(Y* ptr, Deleter del = Deleter(), Alloc alloc = Alloc());

  void swap(SharedPtr& other);

 private:
  template <typename Alloc>
  SharedPtr(MakeSharedControlBlock<T, Alloc>* make_shared_cb);

  SharedPtr(WeakPtr<T> wptr);

  void clear();

  template <typename Y>
  friend class SharedPtr;

  template <typename Y>
  friend class WeakPtr;

  template <typename Y, typename Alloc, typename... Args>
  friend SharedPtr<Y> allocateShared(Alloc, Args&&...);

  template <typename Y, typename... Args>
  friend SharedPtr<Y> makeShared(Args&&...);
};

template <typename T>
SharedPtr<T>::SharedPtr() = default;

template <typename T>
template <typename Deleter, typename Alloc>
SharedPtr<T>::SharedPtr(T* ptr, const Deleter& del, Alloc alloc) : ptr(ptr) {
  using ControlBlockAlloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<ControlBlock<T, Deleter, Alloc>>;
  using ControlBlockAllocTraits = typename std::allocator_traits<
      Alloc>::template rebind_traits<ControlBlock<T, Deleter, Alloc>>;
  ControlBlockAlloc cb_alloc = alloc;
  auto _cb = ControlBlockAllocTraits::allocate(cb_alloc, 1);
  new (_cb) ControlBlock<T, Deleter, Alloc>(1, 0, ptr, del, cb_alloc);
  cb = _cb;
}

template <typename T>
template <typename Y, typename Deleter, typename Alloc>
SharedPtr<T>::SharedPtr(Y* ptr, const Deleter& del, Alloc alloc)
    : ptr(ptr) {
  using ControlBlockAlloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<ControlBlock<Y, Deleter, Alloc>>;
  using ControlBlockAllocTraits = typename std::allocator_traits<
      Alloc>::template rebind_traits<ControlBlock<Y, Deleter, Alloc>>;
  ControlBlockAlloc cb_alloc = alloc;

  auto _cb = ControlBlockAllocTraits::allocate(cb_alloc, 1);
  new (_cb) ControlBlock<Y, Deleter, Alloc>(1, 0, ptr, del, cb_alloc);
  cb = _cb;
}

template <typename T>
template <typename Y>
SharedPtr<T>::SharedPtr(
    const SharedPtr<Y>& shptr) noexcept
    : ptr(shptr.ptr), cb(shptr.cb) {
  ++shptr.cb->shared;
}

template <typename T>
SharedPtr<T>::SharedPtr(const SharedPtr& shptr) noexcept
    : ptr(shptr.ptr), cb(shptr.cb) {
  if (cb != nullptr) {
    ++cb->shared;
  }
}

template <typename T>
SharedPtr<T>::SharedPtr(SharedPtr&& shptr) noexcept
    : ptr(shptr.ptr), cb(shptr.cb) {
  shptr.ptr = nullptr;
  shptr.cb = nullptr;
}

template <typename T>
template <typename Y>
SharedPtr<T>::SharedPtr(
    SharedPtr<Y>&& shptr) noexcept
    : ptr(shptr.ptr), cb(shptr.cb) {
  shptr.ptr = nullptr;
  shptr.cb = nullptr;
}

template <typename T>
SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr& shptr) noexcept {
  if (this == &shptr) {
    return *this;
  }
  SharedPtr copy(shptr);
  swap(copy);
  return *this;
}

template <typename T>
template <typename Y>
SharedPtr<T>& SharedPtr<T>::operator=(
    const SharedPtr<Y>& shptr) noexcept {
  SharedPtr copy(shptr);
  swap(copy);
  return *this;
}

template <typename T>
SharedPtr<T>& SharedPtr<T>::operator=(SharedPtr&& shptr) noexcept {
  SharedPtr copy(std::move(shptr));
  swap(copy);
  return *this;
}

template <typename T>
template <typename Y>
SharedPtr<T>& SharedPtr<T>::operator=(
    SharedPtr<Y>&& shptr) noexcept {
  SharedPtr copy(std::move(shptr));
  swap(copy);
  return *this;
}

template <typename T>
T& SharedPtr<T>::operator*() const noexcept {
  if (ptr) {
    return *ptr;
  }
  return static_cast<MakeSharedControlBlock<T>*>(cb)->get_ptr();
}

template <typename T>
T* SharedPtr<T>::operator->() const noexcept {
  if (ptr) {
    return ptr;
  }
  return &(static_cast<MakeSharedControlBlock<T>*>(cb)->get_ptr());
}

template <typename T>
SharedPtr<T>::~SharedPtr() {
  clear();
}

template <typename T>
size_t SharedPtr<T>::use_count() const {
  if (cb == nullptr)
    return 0;
  return cb->shared;
}

template <typename T>
T* SharedPtr<T>::get() const {
  if (ptr) {
    return ptr;
  }
  if (cb) {
    return &(static_cast<MakeSharedControlBlock<T>*>(cb)->get_ptr());
  }
  return nullptr;
}

template <typename T>
void SharedPtr<T>::reset() {
  SharedPtr<T>().swap(*this);
}

template <typename T>
template <typename Y, typename Deleter, typename Alloc>
void SharedPtr<T>::reset(Y* ptr,
                                                              Deleter del,
                                                              Alloc alloc) {
  SharedPtr<T>(ptr, del, alloc).swap(*this);
}

template <typename T>
void SharedPtr<T>::swap(SharedPtr& other) {
  std::swap(ptr, other.ptr);
  std::swap(cb, other.cb);
}

template <typename T>
template <typename Alloc>
SharedPtr<T>::SharedPtr(MakeSharedControlBlock<T, Alloc>* make_shared_cb)
    : ptr(nullptr), cb(make_shared_cb) {}

template <typename T>
SharedPtr<T>::SharedPtr(WeakPtr<T> wptr) : ptr(wptr.ptr), cb(wptr.cb) {
  if (cb != nullptr) {
    if (cb->shared == 0) {
      ptr = nullptr;
      cb = nullptr;
    } else {
      if (ptr == nullptr && cb != nullptr) {
        ptr = &(static_cast<MakeSharedControlBlock<T>*>(cb)->get_ptr());
      }
      ++cb->shared;
    }
  } else {
    ptr = nullptr;
  }
}

template <typename T>
void SharedPtr<T>::clear() {
  if (cb == nullptr) {
    return;
  }
  --cb->shared;
  if (cb->shared == 0) {
    cb->destroy();
    if (cb->weak == 0) {
      cb->deallocate();
    }
  }
  ptr = nullptr;
  cb = nullptr;
}

template <typename T>
class WeakPtr {
  T* ptr = nullptr;
  BaseControlBlock* cb = nullptr;

 public:
  WeakPtr();

  WeakPtr(const SharedPtr<T>& sp);

  template <typename Y>
  WeakPtr(const SharedPtr<Y>& sp);

  WeakPtr(const WeakPtr& wptr);

  template <typename Y>
  WeakPtr(const WeakPtr<Y>& wptr);

  WeakPtr(WeakPtr&& wptr);

  template <typename Y>
  WeakPtr(WeakPtr<Y>&& wptr);

  WeakPtr& operator=(const WeakPtr& wptr);

  template <typename Y>
  WeakPtr& operator=(
      const WeakPtr<Y>& wptr);

  WeakPtr& operator=(WeakPtr&& wptr);

  template <typename Y>
  WeakPtr& operator=(WeakPtr<Y>&& wptr);

  ~WeakPtr();

  size_t use_count() const noexcept;

  bool expired() const noexcept;

  SharedPtr<T> lock() const noexcept;

 private:
  void swap(WeakPtr& other);

  void clear();

  template <typename Y>
  friend class WeakPtr;

  template <typename Y>
  friend class SharedPtr;
};

template <typename T>
WeakPtr<T>::WeakPtr() = default;

template <typename T>
WeakPtr<T>::WeakPtr(const SharedPtr<T>& sp) : ptr(sp.get()), cb(sp.cb) {
  ++cb->weak;
}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(const SharedPtr<Y>& sp)
    : ptr(sp.get()), cb(sp.cb) {
  if (cb != nullptr) {
    ++sp.cb->weak;
  }
}

template <typename T>
WeakPtr<T>::WeakPtr(const WeakPtr& wptr) : ptr(wptr.ptr), cb(wptr.cb) {
  ++cb->weak;
}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(const WeakPtr<Y>& wptr)
    : ptr(wptr.ptr), cb(wptr.cb) {
  if (cb != nullptr) {
    ++wptr.cb->weak;
  }
}

template <typename T>
WeakPtr<T>::WeakPtr(WeakPtr&& wptr) : ptr(wptr.ptr), cb(wptr.cb) {
  wptr.cb = nullptr;
  wptr.ptr = nullptr;
}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(WeakPtr<Y>&& wptr)
    : ptr(wptr.ptr), cb(wptr.cb) {
  wptr.ptr = nullptr;
  wptr.cb = nullptr;
}

template <typename T>
WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr& wptr) {
  if (this == &wptr) {
    return *this;
  }
  WeakPtr copy(wptr);
  swap(copy);
  return *this;
}

template <typename T>
template <typename Y>
WeakPtr<T>& WeakPtr<T>::operator=(
    const WeakPtr<Y>& wptr) {
  WeakPtr copy(wptr);
  swap(copy);
  return *this;
}

template <typename T>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr&& wptr) {
  WeakPtr copy(std::move(wptr));
  swap(copy);
  return *this;
}

template <typename T>
template <typename Y>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr<Y>&& wptr) {
  WeakPtr copy(std::move(wptr));
  swap(copy);
  return *this;
}

template <typename T>
WeakPtr<T>::~WeakPtr() {
  clear();
}

template <typename T>
size_t WeakPtr<T>::use_count() const noexcept {
  if (cb == nullptr) {
    return 0;
  }
  return cb->shared;
}

template <typename T>
bool WeakPtr<T>::expired() const noexcept {
  return use_count() == 0;
}

template <typename T>
SharedPtr<T> WeakPtr<T>::lock() const noexcept {
  if (expired()) {
    return SharedPtr<T>();
  }
  return SharedPtr<T>(*this);
}

template <typename T>
void WeakPtr<T>::swap(WeakPtr& other) {
  std::swap(ptr, other.ptr);
  std::swap(cb, other.cb);
}

template <typename T>
void WeakPtr<T>::clear() {
  if (cb == nullptr) {
    return;
  }
  --cb->weak;
  if (cb->shared == 0 && cb->weak == 0) {
    cb->deallocate();
  }
  ptr = nullptr;
  cb = nullptr;
}

template <typename T>
class EnableSharedFromThis {
 public:
  SharedPtr<T> shared_from_this() const noexcept { return wptr.lock(); }

 private:
  WeakPtr<T> wptr;

  template <typename Y>
  friend class SharedPtr;
};

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(Alloc alloc, Args&&... args) {
  using ControlBlockAlloc = typename std::allocator_traits<
      Alloc>::template rebind_alloc<MakeSharedControlBlock<T, Alloc>>;
  using ControlBlockAllocTraits = typename std::allocator_traits<
      Alloc>::template rebind_traits<MakeSharedControlBlock<T, Alloc>>;
  ControlBlockAlloc cb_alloc = alloc;
  void* raw_memory = ControlBlockAllocTraits::allocate(cb_alloc, 1);
  auto cb = new (raw_memory) MakeSharedControlBlock<T, Alloc>(
      1, 0, cb_alloc, std::forward<Args>(args)...);
  return SharedPtr<T>(cb);
}

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
  using ControlBlockType = MakeSharedControlBlock<T, std::allocator<T>>;
  using ControlBlockAlloc = std::allocator<ControlBlockType>;
  using ControlBlockAllocTraits = std::allocator_traits<ControlBlockAlloc>;

  ControlBlockAlloc cb_alloc;

  void* raw_memory = ControlBlockAllocTraits::allocate(cb_alloc, 1);

  auto cb = new (raw_memory)
      ControlBlockType(1, 0, cb_alloc, std::forward<Args>(args)...);

  return SharedPtr<T>(cb);
}