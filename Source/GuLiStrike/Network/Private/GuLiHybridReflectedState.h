#pragma once
#include "CoreMinimal.h"

class FGuLiHybridStateArchive;
class UScriptStruct;

/** Bounded, named fields for pure reflected values. Object pointers and runtime handles are rejected. */
class FGuLiHybridReflectedState final
{
public:
    static bool Value(FGuLiHybridStateArchive& Archive,const UScriptStruct* Type,void* Value);
    template<class T> static bool Value(FGuLiHybridStateArchive& Archive,T& Value)
    {return FGuLiHybridReflectedState::Value(Archive,T::StaticStruct(),&Value);}
};
