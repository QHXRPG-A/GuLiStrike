#pragma once
#include "GuLiNetTypes.h"
#include <memory>

namespace GuLiNet
{
/** Capability payload versions are immutable. Copying an unaffected object shares its bytes. */
class SharedBytes
{
    std::shared_ptr<Bytes> Value=std::make_shared<Bytes>();
    Bytes& Write() {if (Value.use_count()!=1) Value=std::make_shared<Bytes>(*Value);return *Value;}
public:
    SharedBytes()=default;
    SharedBytes(const SharedBytes&)=default;
    SharedBytes& operator=(const SharedBytes&)=default;
    SharedBytes(SharedBytes&& Other) noexcept:Value(Other.Value) {}
    SharedBytes& operator=(SharedBytes&& Other) noexcept {Value=Other.Value;return *this;}
    SharedBytes(Bytes Data):Value(std::make_shared<Bytes>(std::move(Data))) {}
    SharedBytes(std::initializer_list<std::uint8_t> Data):Value(std::make_shared<Bytes>(Data)) {}
    SharedBytes& operator=(Bytes Data) {Value=std::make_shared<Bytes>(std::move(Data));return *this;}
    SharedBytes& operator=(std::initializer_list<std::uint8_t> Data) {Value=std::make_shared<Bytes>(Data);return *this;}
    operator const Bytes&() const {return *Value;}
    const std::uint8_t* data() const {return Value->data();}
    auto size() const {return Value->size();}
    bool empty() const {return Value->empty();}
    auto begin() const {return Value->cbegin();}
    auto end() const {return Value->cend();}
    const std::uint8_t& operator[](std::size_t Index) const {return (*Value)[Index];}
    std::uint8_t& operator[](std::size_t Index) {return Write()[Index];}
    const std::uint8_t& back() const {return Value->back();}
    std::uint8_t& back() {return Write().back();}
    void clear() {Value=std::make_shared<Bytes>();}
    void push_back(std::uint8_t Byte) {Write().push_back(Byte);}
    void resize(std::size_t Size) {Write().resize(Size);}
    template<class It> void assign(It Begin,It End) {Value=std::make_shared<Bytes>(Begin,End);}
    friend bool operator==(const SharedBytes& A,const SharedBytes& B) {return A.Value==B.Value||*A.Value==*B.Value;}
};
}
