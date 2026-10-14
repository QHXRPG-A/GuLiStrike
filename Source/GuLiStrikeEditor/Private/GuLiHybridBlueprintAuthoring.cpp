#include "GuLiHybridAuthoringLibrary.h"
#include "GuLiHybridBlueprintSnapshot.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableSet.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

FString UGuLiHybridAuthoringLibrary::InspectProductionBlueprints(const TArray<FString>& AssetPaths)
{
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Assets;
    bool Success=GEditor && !GEditor->PlayWorld && AssetPaths.Num()>0 && AssetPaths.Num()<=512;
    if (!Success) Report->SetStringField(TEXT("error"),TEXT("Requires an idle editor and 1..512 explicit production Blueprint paths."));
    TSet<FString> Seen;
    if (Success) for (const auto& Path:AssetPaths)
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("asset"),Path);TArray<TSharedPtr<FJsonValue>> Findings;
        if (!Path.StartsWith(TEXT("/Game/")) || Seen.Contains(Path)) {Success=false;Entry->SetStringField(TEXT("error"),TEXT("Invalid or duplicate project asset path"));}
        // Loading an unknown package can compile dependent Blueprints even
        // with LOAD_DisableCompileOnLoad. This no-build API reads loaded assets.
        else if (auto* BP=FindObject<UBlueprint>(nullptr,*Path))
        {
            Seen.Add(Path);Entry->SetStringField(TEXT("parent"),BP->ParentClass?BP->ParentClass->GetPathName():TEXT(""));
            Entry->SetStringField(TEXT("generated_class"),BP->GeneratedClass?BP->GeneratedClass->GetPathName():TEXT(""));
            Entry->SetBoolField(TEXT("package_dirty"),BP->GetOutermost()->IsDirty());
            Entry->SetNumberField(TEXT("blueprint_status"),uint8(BP->Status));
            const auto Add=[&](const FString& Reason,const UEdGraph* Graph,const UEdGraphNode* Node,const FString& Member)
            {
                auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("reason"),Reason);V->SetStringField(TEXT("member"),Member);
                V->SetStringField(TEXT("graph"),Graph?Graph->GetPathName():TEXT(""));
                V->SetStringField(TEXT("node"),Node?Node->NodeGuid.ToString(EGuidFormats::Digits):TEXT(""));
                Findings.Add(MakeShared<FJsonValueObject>(V));
            };
            for (const auto& Variable:BP->NewVariables)
                if ((Variable.PropertyFlags&CPF_Net)!=0) Add(TEXT("Blueprint replicated variable requires an authority/capability audit"),nullptr,nullptr,Variable.VarName.ToString());
            TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
            Graphs.Sort([](const UEdGraph& A,const UEdGraph& B){return A.GetPathName()<B.GetPathName();});
            TArray<TSharedPtr<FJsonValue>> GraphDetails;
            for (const auto* Graph:Graphs) if (Graph) GraphDetails.Add(MakeShared<FJsonValueObject>(GuLiHybridAuthoring::ReadGraph(*Graph)));
            Entry->SetArrayField(TEXT("graphs"),GraphDetails);
            for (const auto* Graph:Graphs) if (Graph) for (const UEdGraphNode* Node:Graph->Nodes) if (Node)
            {
                if (const auto* Call=Cast<UK2Node_CallFunction>(Node))
                {
                    const auto* Function=Call->GetTargetFunction();const auto Name=Call->FunctionReference.GetMemberName();
                    if (Name==TEXT("HasAuthority") || Name==TEXT("IsServer") || Name==TEXT("IsDedicatedServer"))
                        Add(TEXT("Process authority gate: classify simulation, presentation or ledger before replacing"),Graph,Node,Name.ToString());
                    if (Function && Function->HasAnyFunctionFlags(FUNC_Net))
                        Add(TEXT("RPC call: verify that the reflected entry routes through Hybrid"),Graph,Node,Function->GetPathName());
                }
                if (const auto* Event=Cast<UK2Node_CustomEvent>(Node);Event && (Event->GetNetFlags()&FUNC_Net)!=0)
                    Add(TEXT("Blueprint RPC event requires migration"),Graph,Node,Event->CustomFunctionName.ToString());
                if (const auto* Set=Cast<UK2Node_VariableSet>(Node))
                    Add(TEXT("Field write: assign to simulation state, ledger application or presentation"),Graph,Node,Set->VariableReference.GetMemberName().ToString());
                if (Node->GetClass()->GetName().Contains(TEXT("SwitchHasAuthority")))
                    Add(TEXT("Process authority switch requires object authority migration"),Graph,Node,Node->GetClass()->GetName());
                if (const auto* Macro=Cast<UK2Node_MacroInstance>(Node);Macro && Macro->GetMacroGraph()
                    && Macro->GetMacroGraph()->GetName().Contains(TEXT("HasAuthority")))
                    Add(TEXT("Process authority macro requires object authority migration"),Graph,Node,Macro->GetMacroGraph()->GetPathName());
            }
        }
        else {Success=false;Entry->SetStringField(TEXT("error"),TEXT("Blueprint is not loaded; no implicit load or compilation was attempted"));}
        Entry->SetArrayField(TEXT("review_items"),Findings);Assets.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetBoolField(TEXT("success"),Success);Report->SetBoolField(TEXT("modified_assets"),false);
    Report->SetStringField(TEXT("schema"),TEXT("guli-production-blueprint-inspection/v2"));
    Report->SetBoolField(TEXT("migration_complete"),false);Report->SetBoolField(TEXT("compiled"),false);
    Report->SetArrayField(TEXT("assets"),Assets);FString Result;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Result));return Result;
}
