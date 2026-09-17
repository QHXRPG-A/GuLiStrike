#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;

/** Stable editor-side source hash shared by ground and flight bake validation. */
class GULIFLIGHTNAVIGATIONEDITOR_API FGuLiNavigationSourceHash
{
public:
	void AddBytes(const void* Data, int64 Size);
	void AddUInt64(uint64 Value);
	void AddString(const FString& Value);
	void AddVector(const FVector& Value);
	void AddTransform(const FTransform& Value);
	void AddProperty(const UObject* Object, const TCHAR* Name);
	void AddCollisionSource(const UPrimitiveComponent* Component);
	uint64 Get() const { return State ? State : 1; }

private:
	uint64 State = 14695981039346656037ull;
};
