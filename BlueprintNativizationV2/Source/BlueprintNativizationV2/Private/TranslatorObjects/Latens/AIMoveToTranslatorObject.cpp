#include "TranslatorObjects/Latens/AIMoveToTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "EdGraphSchema_K2.h"

void UAIMoveToTranslatorObject::GenerateNewFunction(UK2Node* Node, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& Entries, UNativizationV2Subsystem* Subsystem)
{
    for (const TCHAR* Outcome : {TEXT("OnSuccess"), TEXT("OnFail")})
    {
        FGenerateFunctionStruct Entry;
        Entry.Node = Node;
        Entry.Name = *Subsystem->GetNodeStateName(Node, FString(TEXT("MoveCompleted")) + Outcome);
        Entry.bIsLatentContinuation = true;
        Entry.bRequiresReflection = true;
        Entry.EntryPinName = Outcome;
        Entry.MacroStack = Path;
        Entry.CustomParametersString.Add(TEXT("EPathFollowingResult::Type ") +
            Subsystem->GetNodeStateName(Node, TEXT("MoveCallbackMovementResult")));
        Entries.AddUnique(Entry);
    }
}

FString UAIMoveToTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    if (EntryPinName == TEXT("OnSuccess") || EntryPinName == TEXT("OnFail"))
    {
        FString Content;
        if (UEdGraphPin* Result = Node->FindPin(TEXT("MovementResult")); Result && !Result->LinkedTo.IsEmpty())
        {
            Content += TEXT("this->") + UBlueprintNativizationLibrary::GetUniquePinName(Result, Subsystem->EntryNodes) + TEXT(" = ") +
                Subsystem->GetNodeStateName(Node, TEXT("MoveCallbackMovementResult")) + TEXT(";\n");
        }
        if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(*EntryPinName), 0, false, MacroStack, Subsystem))
        {
            Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
        }
        return Content;
    }
    FGenerationPreparations Inputs;
    TArray<FString> Arguments = {TEXT("this")};
    for (const TCHAR* Name : {TEXT("Pawn"), TEXT("Destination"), TEXT("TargetActor"), TEXT("AcceptanceRadius"), TEXT("bStopOnOverlap")})
    {
        const FGenerateResultStruct Input = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPinChecked(Name), 0, MacroStack);
        Inputs.Append(Input.Preparations);
        Arguments.Add(Input.Code);
    }
    FString Content = GenerateNewPreparations(Preparations, Inputs);
    Preparations.Append(Inputs);
    const FString Proxy = Subsystem->GetNodeStateName(Node, TEXT("MoveTask"), true);
    Content += TEXT("if (auto* ") + Proxy + TEXT(" = UAIBlueprintHelperLibrary::CreateMoveToProxyObject(") + FString::Join(Arguments, TEXT(", ")) + TEXT("))\n{\n");
    UBlueprint* Blueprint = FGenerationGraphContext::GetBlueprint();
    const FString Class = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    for (const TCHAR* Outcome : {TEXT("OnSuccess"), TEXT("OnFail")})
    {
        const FString Callback = Subsystem->GetNodeStateName(Node, FString(TEXT("MoveCompleted")) + Outcome);
        Content += Proxy + TEXT("->") + Outcome + TEXT(".AddDynamic(this, &") + Class + TEXT("::") + Callback + TEXT(");\n");
    }
    Content += TEXT("}\n");
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}

TSet<FString> UAIMoveToTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Blueprint/AIBlueprintHelperLibrary.h\""));
    Includes.Add(TEXT("#include \"Blueprint/AIAsyncTaskBlueprintProxy.h\""));
    return Includes;
}

TSet<FString> UAIMoveToTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return {TEXT("#include \"Navigation/PathFollowingComponent.h\"")};
}

FString UAIMoveToTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    for (UEdGraphPin* Output : Node->Pins)
    {
        if (Output->Direction == EGPD_Output && Output->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !Output->LinkedTo.IsEmpty())
        {
            Content += TEXT("UPROPERTY(Transient)\n") + UBlueprintNativizationLibrary::GetPinType(Output->PinType, false) + TEXT(" ") +
                UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes) + TEXT("{};\n");
        }
    }
    return Content;
}

FGenerateResultStruct UAIMoveToTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return TEXT("this->") + UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
}
