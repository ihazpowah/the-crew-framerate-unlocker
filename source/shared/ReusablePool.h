#pragma once
#include <deque>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <iterator>
#include <utility>
#include <stdexcept>

template<class T> struct ResetPooledValue { void operator()(T& value) const { value=T{}; } };

// Resident storage is retained after retirement. Growing the deque never moves
// live values; the compact order table preserves the existing logical indices.
template<class T> class ReusableSlots
{
    std::deque<T> storage;
    std::vector<size_t> order, freeSlots;
public:
    class iterator
    {
        ReusableSlots* owner; size_t index;
    public:
        iterator(ReusableSlots* p, size_t i) : owner(p), index(i) {}
        T& operator*() const { return (*owner)[index]; }
        T* operator->() const { return &**this; }
        iterator& operator++() { ++index; return *this; }
        iterator operator+(size_t n) const { return {owner,index+n}; }
        bool operator!=(const iterator& b) const { return index!=b.index; }
        friend class ReusableSlots;
    };
    ReusableSlots() = default;
    ReusableSlots(const ReusableSlots& other) { for(size_t i=0;i<other.size();++i) emplace_back()=other[i]; }
    ReusableSlots& operator=(const ReusableSlots& other)
    { if(this!=&other) { resize(other.size()); for(size_t i=0;i<size();++i) (*this)[i]=other[i]; } return *this; }
    ReusableSlots(ReusableSlots&&) = default;
    ReusableSlots& operator=(ReusableSlots&&) = default;
    size_t size() const { return order.size(); }
    bool empty() const { return order.empty(); }
    size_t capacity() const { return storage.size(); }
    void reserve(size_t n) { order.reserve(n); freeSlots.reserve(n); }
    T& operator[](size_t i) { return storage[order[i]]; }
    const T& operator[](size_t i) const { return storage[order[i]]; }
    T& front() { return (*this)[0]; }
    T& back() { return (*this)[size()-1]; }
    const T& front() const { return (*this)[0]; }
    const T& back() const { return (*this)[size()-1]; }
    iterator begin() { return {this,0}; }
    iterator end() { return {this,size()}; }
    T& emplace_back()
    {
        // Reserve bookkeeping before modifying the pool, including retirement
        // space so erase/clear never allocate.
        if(order.capacity()<order.size()+1) order.reserve(std::max(size_t(8),order.capacity()*2));
        if(freeSlots.capacity()<storage.size()+1) freeSlots.reserve(std::max(size_t(8),freeSlots.capacity()*2));
        size_t slot;
        if(freeSlots.empty()) { slot=storage.size(); storage.emplace_back(); }
        else { slot=freeSlots.back(); ResetPooledValue<T>{}(storage[slot]); freeSlots.pop_back(); }
        order.push_back(slot); return storage[slot];
    }
    iterator erase(iterator pos)
    { freeSlots.push_back(order[pos.index]); order.erase(order.begin()+pos.index); return {this,pos.index}; }
    void pop_back() { erase({this,size()-1}); }
    void clear() { while(!empty()) pop_back(); }
    void resize(size_t n) { while(size()>n) pop_back(); while(size()<n) emplace_back(); }
    size_t index_of(const T* value) const
    { for(size_t i=0;i<size();++i) if(&(*this)[i]==value) return i; return size(); }
};

// Hash lookup contains only stable slot IDs. Iteration skips retired slots and
// erasure recycles their storage instead of allocating a new history next time.
template<class Key, class T, class Hash, class Reset=ResetPooledValue<T>> class ReusableHistoryMap
{
    struct Node { std::pair<Key,T> value{}; bool live=false; };
    std::deque<Node> nodes;
    std::vector<size_t> freeSlots;
    std::unordered_map<Key,size_t,Hash> lookup;
    std::vector<typename decltype(lookup)::node_type> idleLookupNodes;
public:
    class iterator
    {
        ReusableHistoryMap* owner; size_t index;
        void skip() { while(index<owner->nodes.size() && !owner->nodes[index].live) ++index; }
    public:
        iterator(ReusableHistoryMap* p,size_t i):owner(p),index(i) { skip(); }
        auto& operator*() const { return owner->nodes[index].value; }
        auto* operator->() const { return &**this; }
        iterator& operator++() { ++index; skip(); return *this; }
        bool operator==(const iterator& b) const { return owner==b.owner && index==b.index; }
        bool operator!=(const iterator& b) const { return !(*this==b); }
        friend class ReusableHistoryMap;
    };
    size_t size() const { return lookup.size(); }
    bool empty() const { return lookup.empty(); }
    size_t capacity() const { return nodes.size(); }
    size_t count(const Key& key) const { return lookup.count(key); }
    iterator begin() { return {this,0}; }
    iterator end() { return {this,nodes.size()}; }
    iterator find(const Key& key) { auto it=lookup.find(key); return it==lookup.end()?end():iterator(this,it->second); }
    T& at(const Key& key) { return nodes[lookup.at(key)].value.second; }
    std::pair<iterator,bool> emplace(const Key& key)
    {
        auto found=find(key); if(found!=end()) return {found,false};
        if(freeSlots.capacity()<nodes.size()+1) freeSlots.reserve(std::max(size_t(8),freeSlots.capacity()*2));
        if(idleLookupNodes.capacity()<nodes.size()+1) idleLookupNodes.reserve(std::max(size_t(8),idleLookupNodes.capacity()*2));
        const bool recycled=!freeSlots.empty();
        const size_t slot=recycled?freeSlots.back():nodes.size();
        if(!recycled) nodes.emplace_back();
        auto& node=nodes[slot];
        try
        {
            node.value.first=key; Reset{}(node.value.second);
            if(idleLookupNodes.empty()) lookup.emplace(key,slot);
            else
            {
                auto& indexNode=idleLookupNodes.back();
                indexNode.key()=key; indexNode.mapped()=slot;
                lookup.insert(std::move(indexNode)); idleLookupNodes.pop_back();
            }
        }
        catch(...) { if(!recycled) freeSlots.push_back(slot); throw; }
        if(recycled) freeSlots.pop_back();
        node.live=true;
        return {iterator(this,slot),true};
    }
    std::pair<iterator,bool> emplace(const Key& key,T value)
    { auto result=emplace(key); if(result.second) result.first->second=std::move(value); return result; }
    T& operator[](const Key& key) { auto found=find(key); return found!=end()?found->second:emplace(key).first->second; }
    iterator erase(iterator it)
    { const size_t slot=it.index; idleLookupNodes.push_back(lookup.extract(nodes[slot].value.first)); nodes[slot].live=false; freeSlots.push_back(slot); return {this,slot+1}; }
    size_t erase(const Key& key) { auto it=find(key); if(it==end()) return 0; erase(it); return 1; }
    void clear()
    {
        while(!lookup.empty()) idleLookupNodes.push_back(lookup.extract(lookup.begin()));
        freeSlots.clear();
        for(size_t i=0;i<nodes.size();++i) { nodes[i].live=false; freeSlots.push_back(i); }
    }
};
