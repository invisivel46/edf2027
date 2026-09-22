#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace edf::native {
// Chunked copy-on-write sequence. A copy shares every chunk, so copying is O(1)
// and a later write clones only the spine and the chunks it touches. Storage
// reachable from two copies is never written: other threads may read any copy
// they hold while the producer keeps changing its own.
template<class T,size_t Chunk=64>
class NativeSharedVector {
  using Leaf=std::vector<T>;
  struct Spine { std::vector<std::shared_ptr<Leaf>> leaves; size_t size=0; };
 public:
  class iterator {
   public:
    using iterator_category=std::forward_iterator_tag;
    using value_type=T; using difference_type=std::ptrdiff_t;
    using pointer=const T*; using reference=const T&;
    iterator()=default;
    reference operator*() const { return (*spine_->leaves[leaf_])[at_]; }
    pointer operator->() const { return &**this; }
    iterator& operator++() { if(++at_==spine_->leaves[leaf_]->size()) { ++leaf_; at_=0; } return *this; }
    iterator operator++(int) { auto old=*this; ++*this; return old; }
    bool operator==(const iterator& other) const { return leaf_==other.leaf_ && at_==other.at_; }
   private:
    friend class NativeSharedVector;
    iterator(const Spine* spine,size_t leaf):spine_(spine),leaf_(leaf) {}
    const Spine* spine_=nullptr; size_t leaf_=0,at_=0;
  };
  using const_iterator=iterator; using value_type=T;
  NativeSharedVector()=default;
  NativeSharedVector(std::initializer_list<T> values) { for(const auto& value:values) push_back(value); }
  NativeSharedVector(const std::vector<T>& values) { for(const auto& value:values) push_back(value); }
  iterator begin() const { return iterator(spine_.get(),0); }
  iterator end() const { return iterator(spine_.get(),Leaves()); }
  size_t size() const { return spine_?spine_->size:0; }
  bool empty() const { return !size(); }
  const T& operator[](size_t index) const {
    if(spine_) for(const auto& leaf:spine_->leaves) { if(index<leaf->size()) return (*leaf)[index]; index-=leaf->size(); }
    throw std::out_of_range("native shared vector index");
  }
  const T& front() const { return (*this)[0]; }
  // Identity of the shared storage: equal copies were never written apart.
  bool Shares(const NativeSharedVector& other) const { return spine_==other.spine_; }
  void clear() { spine_.reset(); }
  void push_back(T value) {
    auto& spine=MutableSpine();
    if(spine.leaves.empty() || spine.leaves.back()->size()>=Chunk) {
      auto leaf=std::make_shared<Leaf>(); leaf->reserve(Chunk); leaf->push_back(std::move(value));
      spine.leaves.push_back(std::move(leaf));
    } else MutableLeaf(spine,spine.leaves.size()-1).push_back(std::move(value));
    ++spine.size;
  }
  // Sorted use: the caller keeps elements unique and ordered by key_of.
  template<class Key,class KeyOf> const T* Find(const Key& key,const KeyOf& key_of) const {
    const auto [leaf,at]=Locate(key,key_of);
    if(leaf==Leaves()) return nullptr;
    const auto& value=(*spine_->leaves[leaf])[at];
    return key<key_of(value)?nullptr:&value;
  }
  // Null when absent. Clones only the chunk holding key; invalidated by any write.
  template<class Key,class KeyOf> T* Mutable(const Key& key,const KeyOf& key_of) {
    const auto [leaf,at]=Locate(key,key_of);
    if(leaf==Leaves() || key<key_of((*spine_->leaves[leaf])[at])) return nullptr;
    return &MutableLeaf(MutableSpine(),leaf)[at];
  }
  // Inserts value, or replaces the element with the same key.
  template<class KeyOf> void Assign(T value,const KeyOf& key_of) {
    const auto key=key_of(value);
    auto [leaf,at]=Locate(key,key_of);
    if(!Leaves()) { push_back(std::move(value)); return; }
    auto& spine=MutableSpine();
    if(leaf==spine.leaves.size()) { leaf=spine.leaves.size()-1; at=spine.leaves[leaf]->size(); }
    auto& values=MutableLeaf(spine,leaf);
    if(at<values.size() && !(key<key_of(values[at]))) { values[at]=std::move(value); return; }
    values.insert(values.begin()+at,std::move(value)); ++spine.size;
    if(values.size()>=2*Chunk) {
      auto upper=std::make_shared<Leaf>(std::make_move_iterator(values.begin()+Chunk),std::make_move_iterator(values.end()));
      spine.leaves.insert(spine.leaves.begin()+leaf+1,std::move(upper));
      values.erase(values.begin()+Chunk,values.end());
    }
  }
  template<class Key,class KeyOf> bool Erase(const Key& key,const KeyOf& key_of) {
    const auto [leaf,at]=Locate(key,key_of);
    if(leaf==Leaves() || key<key_of((*spine_->leaves[leaf])[at])) return false;
    auto& spine=MutableSpine();
    auto& values=MutableLeaf(spine,leaf);
    values.erase(values.begin()+at); --spine.size;
    if(values.empty()) spine.leaves.erase(spine.leaves.begin()+leaf);
    return true;
  }
  template<class Predicate> size_t EraseIf(Predicate predicate) {
    size_t erased=0;
    for(size_t leaf=0;leaf<Leaves();) {
      const auto& current=*spine_->leaves[leaf];
      if(std::none_of(current.begin(),current.end(),[&](const T& value) { return bool(predicate(value)); })) { ++leaf; continue; }
      auto& spine=MutableSpine();
      auto& values=MutableLeaf(spine,leaf);
      const auto removed=size_t(std::erase_if(values,[&](const T& value) { return bool(predicate(value)); }));
      erased+=removed; spine.size-=removed;
      if(values.empty()) spine.leaves.erase(spine.leaves.begin()+leaf); else ++leaf;
    }
    return erased;
  }
 private:
  std::shared_ptr<Spine> spine_;
  size_t Leaves() const { return spine_?spine_->leaves.size():0; }
  // Lower bound of key: its leaf and position, or {Leaves(),0} past the end.
  template<class Key,class KeyOf> std::pair<size_t,size_t> Locate(const Key& key,const KeyOf& key_of) const {
    if(!spine_) return {0,0};
    const auto& leaves=spine_->leaves;
    const auto leaf=size_t(std::partition_point(leaves.begin(),leaves.end(),
      [&](const auto& values) { return key_of(values->back())<key; })-leaves.begin());
    if(leaf==leaves.size()) return {leaf,0};
    const auto& values=*leaves[leaf];
    return {leaf,size_t(std::partition_point(values.begin(),values.end(),
      [&](const T& value) { return key_of(value)<key; })-values.begin())};
  }
  // Only the producer creates references to storage it owns alone, so a count
  // of one cannot rise concurrently. The fence orders our writes after reads
  // made by the owner whose release brought the count down to one.
  template<class U> static bool Exclusive(const std::shared_ptr<U>& value) {
    if(value.use_count()!=1) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    return true;
  }
  Spine& MutableSpine() {
    if(!spine_) spine_=std::make_shared<Spine>();
    else if(!Exclusive(spine_)) spine_=std::make_shared<Spine>(*spine_);
    return *spine_;
  }
  static Leaf& MutableLeaf(Spine& spine,size_t leaf) {
    auto& values=spine.leaves[leaf];
    if(!Exclusive(values)) values=std::make_shared<Leaf>(*values);
    return *values;
  }
};
// Sorted map over NativeSharedVector with the same sharing guarantees.
template<class K,class V,size_t Chunk=64>
class NativeSharedMap {
  struct First { const K& operator()(const std::pair<K,V>& value) const { return value.first; } };
 public:
  using value_type=std::pair<K,V>;
  auto begin() const { return values_.begin(); }
  auto end() const { return values_.end(); }
  size_t size() const { return values_.size(); }
  bool empty() const { return values_.empty(); }
  const V* Find(const K& key) const { const auto* found=values_.Find(key,First{}); return found?&found->second:nullptr; }
  bool contains(const K& key) const { return values_.Find(key,First{})!=nullptr; }
  size_t count(const K& key) const { return contains(key)?1:0; }
  const V& at(const K& key) const {
    if(const auto* found=Find(key)) return *found;
    throw std::out_of_range("native shared map key");
  }
  V* Mutable(const K& key) { auto* found=values_.Mutable(key,First{}); return found?&found->second:nullptr; }
  void Set(K key,V value) { values_.Assign(value_type(std::move(key),std::move(value)),First{}); }
  bool Erase(const K& key) { return values_.Erase(key,First{}); }
  template<class Predicate> size_t EraseIf(Predicate predicate) { return values_.EraseIf(predicate); }
  bool Shares(const NativeSharedMap& other) const { return values_.Shares(other.values_); }
 private:
  NativeSharedVector<value_type,Chunk> values_;
};
}
