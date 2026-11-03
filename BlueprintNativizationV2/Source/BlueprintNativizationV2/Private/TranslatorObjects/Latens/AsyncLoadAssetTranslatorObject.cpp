#include "TranslatorObjects/Latens/AsyncLoadAssetTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "EdGraphSchema_K2.h"

void UAsyncLoadAssetTranslatorObject::GenerateNewFunction(UK2Node* Node, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& Entries, UNativizationV2Subsystem* Subsystem)
{
    FGenerateFunctionStruct Entry;
    Entry.Node = Node;
    Entry.Name = *Subsystem->GetNodeStateName(Node, TEXT("AssetLoaded"));
    Entry.bIsLatentContinuation = true;
    Entry.EntryPinName = TEXT("Completed");
    Entry.MacroStack = Path;
    Entries.AddUnique(Entry);
}

FString UAsyncLoadAssetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    if (EntryPinName == TEXT("Completed"))
    {
        if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("Completed")), 0, false, MacroStack, Subsystem))
        {
            return Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
        }
        return {};
    }
    const bool bClass = Node->IsA<UK2Node_LoadAssetClass>();
    const bool bArray = Node->IsA<UK2Node_LoadAssets>();
    UEdGraphPin* Input = Node->FindPin(TEXT("Asset"));
    UEdGraphPin* Output = Node->FindPin(TEXT("Object"));
    if (bClass)
    {
        Input = Node->FindPin(TEXT("AssetClass"));
        Output = Node->FindPin(TEXT("Class"));
    }
    else if (bArray)
    {
        Input = Node->FindPin(TEXT("Assets"));
        Output = Node->FindPin(TEXT("Objects"));
    }
    const FGenerateResultStruct Asset = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
    FString Content = GenerateNewPreparations(Preparations, Asset.Preparations);
    Preparations.Append(Asset.Preparations);
    const FString State = Subsystem->GetNodeStateName(Node, TEXT("AssetLoad"));
    const FString Completed = Subsystem->GetNodeStateName(Node, TEXT("AssetLoaded"));
    Content += TEXT("{\nTArray<FSoftObjectPath> Paths;\n");
    if (bArray)
    {
        Content += TEXT("for (const auto& Asset : ") + Asset.Code + TEXT(")\n{\nPaths.Add(Asset.ToSoftObjectPath());\n}\n");
    }
    else
    {
        Content += TEXT("Paths.Add((") + Asset.Code + TEXT(").ToSoftObjectPath());\n");
    }
    Content += TEXT("TArray<FSoftObjectPath> RequestedPaths = Paths;\nRequestedPaths.RemoveAll([](const FSoftObjectPath& Path)\n{\nreturn Path.IsNull();\n});\nTSharedPtr<FStreamableHandle> Loading;\nif (!RequestedPaths.IsEmpty())\n{\nLoading = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedPaths);\n}\n");
    Content += State + TEXT("Requests.Add([this, Paths, Loading]() mutable -> bool\n{\nif (Loading && !Loading->HasLoadCompleted() && !Loading->WasCanceled())\n{\nreturn false;\n}\n");
    if (!Output->LinkedTo.IsEmpty())
    {
        const FString Result = UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes);
        if (bArray)
        {
            Content += Result + TEXT(".Reset();\nfor (const FSoftObjectPath& Path : Paths)\n{\n") + Result + TEXT(".Add(Path.ResolveObject());\n}\n");
        }
        else if (bClass)
        {
            Content += Result + TEXT(" = Cast<UClass>(Paths[0].ResolveObject());\n");
        }
        else
        {
            Content += Result + TEXT(" = Paths[0].ResolveObject();\n");
        }
    }
    Content += Completed + TEXT("();\nreturn true;\n});\nif (!") + State + TEXT("Pending)\n{\n") + State + TEXT("Pending = true;\n") +
        State + TEXT("Update = FTimerDelegate::CreateWeakLambda(this, [this]()\n{\nauto CurrentRequests = MoveTemp(") + State + TEXT("Requests);\nfor (auto& Request : CurrentRequests)\n{\nif (!Request())\n{\n") +
        State + TEXT("Requests.Add(MoveTemp(Request));\n}\n}\nif (") + State + TEXT("Requests.IsEmpty())\n{\n") + State + TEXT("Pending = false;\n") + State + TEXT("Update.Unbind();\n}\nelse\n{\nGetWorld()->GetTimerManager().SetTimerForNextTick(") + State + TEXT("Update);\n}\n});\nGetWorld()->GetTimerManager().SetTimerForNextTick(") + State + TEXT("Update);\n}\n}\n");
    TArray<UK2Node*> ImmediatePath = MacroStack;
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, ImmediatePath, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, ImmediatePath);
    }
    return Content;
}

FGenerateResultStruct UAsyncLoadAssetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
}

TSet<FString> UAsyncLoadAssetTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Engine/StreamableManager.h\""));
    Includes.Add(TEXT("#include \"Engine/AssetManager.h\""));
    Includes.Add(TEXT("#include \"Engine/World.h\""));
    Includes.Add(TEXT("#include \"TimerManager.h\""));
    return Includes;
}

FString UAsyncLoadAssetTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    const FString State = Subsystem->GetNodeStateName(Node, TEXT("AssetLoad"));
    FString Content = TEXT("TArray<TFunction<bool()>> ") + State + TEXT("Requests;\nbool ") + State + TEXT("Pending = false;\nFTimerDelegate ") + State + TEXT("Update;\n");
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !Pin->LinkedTo.IsEmpty())
        {
            Content += TEXT("UPROPERTY(Transient)\n") + UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false) + TEXT(" ") +
                UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes) + TEXT("{};\n");
        }
    }
    return Content;
}

TSet<FString> UAsyncLoadAssetTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return {TEXT("#include \"TimerManager.h\"")};
}
