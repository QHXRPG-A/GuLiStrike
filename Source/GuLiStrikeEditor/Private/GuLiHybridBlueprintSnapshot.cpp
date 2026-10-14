#include "GuLiHybridBlueprintSnapshot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Variable.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

TSharedRef<FJsonObject> GuLiHybridAuthoring::ReadGraph(const UEdGraph& Graph)
{
    auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("graph"),Graph.GetName());
    Result->SetStringField(TEXT("path"),Graph.GetPathName());
    TArray<const UEdGraphNode*> Nodes;for (const UEdGraphNode* N:Graph.Nodes) if (N) Nodes.Add(N);
    Nodes.Sort([](const UEdGraphNode& A,const UEdGraphNode& B){return A.NodeGuid.ToString()<B.NodeGuid.ToString();});
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const auto* Node:Nodes)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("guid"),Node->NodeGuid.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("name"),Node->GetName());J->SetStringField(TEXT("class"),Node->GetClass()->GetPathName());
        J->SetStringField(TEXT("comment"),Node->NodeComment);J->SetNumberField(TEXT("enabled"),uint8(Node->GetDesiredEnabledState()));
        if (const auto* Call=Cast<UK2Node_CallFunction>(Node))
        {
            const auto* F=Call->GetTargetFunction();J->SetStringField(TEXT("function"),F?F->GetPathName():TEXT(""));
            J->SetStringField(TEXT("member"),Call->FunctionReference.GetMemberName().ToString());
            J->SetStringField(TEXT("function_flags"),F?LexToString(uint32(F->FunctionFlags)):TEXT("unresolved"));
        }
        if (const auto* Event=Cast<UK2Node_CustomEvent>(Node)) J->SetStringField(TEXT("net_flags"),LexToString(uint32(Event->GetNetFlags())));
        if (const auto* Variable=Cast<UK2Node_Variable>(Node)) J->SetStringField(TEXT("variable"),Variable->VariableReference.GetMemberName().ToString());
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const auto* Pin:Node->Pins) if (Pin)
        {
            auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("name"),Pin->PinName.ToString());
            P->SetStringField(TEXT("id"),Pin->PinId.ToString(EGuidFormats::Digits));P->SetNumberField(TEXT("direction"),uint8(Pin->Direction));
            FString Type;FEdGraphPinType::StaticStruct()->ExportText(Type,&Pin->PinType,nullptr,nullptr,PPF_None,nullptr);
            P->SetStringField(TEXT("type"),Type);P->SetBoolField(TEXT("hidden"),Pin->bHidden);P->SetBoolField(TEXT("orphaned"),Pin->bOrphanedPin);
            P->SetStringField(TEXT("default"),Pin->DefaultValue);P->SetStringField(TEXT("default_text"),Pin->DefaultTextValue.ToString());
            P->SetStringField(TEXT("default_object"),Pin->DefaultObject?Pin->DefaultObject->GetPathName():TEXT(""));
            TArray<FString> Links;for (const auto* Other:Pin->LinkedTo) if (Other && Other->GetOwningNode())
                Links.Add(Other->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits)+TEXT("/")+Other->PinId.ToString(EGuidFormats::Digits));
            Links.Sort();TArray<TSharedPtr<FJsonValue>> L;for (const auto& Link:Links) L.Add(MakeShared<FJsonValueString>(Link));
            P->SetArrayField(TEXT("links"),L);Pins.Add(MakeShared<FJsonValueObject>(P));
        }
        J->SetArrayField(TEXT("pins"),Pins);Items.Add(MakeShared<FJsonValueObject>(J));
    }
    Result->SetArrayField(TEXT("nodes"),Items);FString Canonical;
    FJsonSerializer::Serialize(Result,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Canonical));
    Result->SetStringField(TEXT("fingerprint"),FMD5::HashAnsiString(*Canonical));return Result;
}
