#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "GuLiConstructionShapeSubsystem.generated.h"

class UFactory;

/** Coalesced, reference-driven footprint maintenance; never scans the content directory. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiConstructionShapeSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	void ObjectChanged(UObject* Object);
	void Imported(UFactory* Factory, UObject* Object);
	void BlueprintCompiled();
	void BeforePIE(bool bSimulate);
	bool Tick(float Delta);
	void Prepare();
	bool bPending = true;
	bool bPreparing = false;
	FDelegateHandle ImportHandle, ReimportHandle, SaveHandle, BlueprintHandle, PIEHandle;
	FTSTicker::FDelegateHandle TickHandle;
};
