#pragma once
#include <algorithm>
#include <cstddef>
extern "C" void *gfxbench_alloc(std::size_t bytes);
extern "C" void gfxbench_free(void *pointer);
/* Fixed capacity, checked once before use. No STL heap or FreeRTOS locks. */
template<class T,std::size_t Capacity> class NativeBuffer {
    T *pointer_ = static_cast<T *>(gfxbench_alloc(sizeof(T)*Capacity));
    std::size_t count_=0;
public:
    ~NativeBuffer() {gfxbench_free(pointer_);}
    NativeBuffer()=default;
    NativeBuffer(const NativeBuffer&)=delete;
    NativeBuffer& operator=(const NativeBuffer&)=delete;
    bool valid() const {return pointer_!=nullptr;}
    void resize(std::size_t n) {count_=n;}
    void assign(std::size_t n,const T& value) {resize(n);std::fill(begin(),end(),value);}
    T *data() {return pointer_;}
    T *begin() {return pointer_;}
    T *end() {return pointer_+count_;}
    std::size_t size() const {return count_;}
    T& operator[](std::size_t n) {return pointer_[n];}
};
