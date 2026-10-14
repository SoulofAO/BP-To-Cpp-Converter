#include "TranslatorObjects/DoOnceMultiInputTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Blueprint.h"


FString UDoOnceMultiInputTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return FString();
    }
    return FString::Format(TEXT("bool {0} = false;\n"), { Subsystem->GetNodeStateName(InputNode, TEXT("bDoOnceCompleted")) });
}

TSet<FString> UDoOnceMultiInputTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return { FString::Format(TEXT("bool {0} = false;\n"), { Subsystem->GetNodeStateName(InputNode, TEXT("bDoOnceCompleted")) }) };
    }
    return {};
}

FString UDoOnceMultiInputTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_DoOnceMultiInput* DoOnce = CastChecked<UK2Node_DoOnceMultiInput>(Node);
    const FString StateName = Subsystem->GetNodeStateName(Node, TEXT("bDoOnceCompleted"));
    for (int32 InputIndex = 0; InputIndex < DoOnce->NumAdditionalInputs + 2; ++InputIndex)
    {
        if (DoOnce->GetInputPin(InputIndex)->GetName() != EntryPinName)
        {
            continue;
        }
        UEdGraphPin* Output = DoOnce->GetOutputPin(InputIndex);
        UEdGraphPin* ConnectedPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Output, 0, false, MacroStack, Subsystem);
        FString Branch;
        if (ConnectedPin)
        {
            Branch = Subsystem->GenerateCodeFromNode(Cast<UK2Node>(ConnectedPin->GetOwningNode()), ConnectedPin->GetName(),
                VisitedNodes, Preparations, MacroStack);
        }
        if (EntryPinName == TEXT("Reset In"))
        {
            return FString::Format(TEXT("{0} = false;\n{1}"), { StateName, Branch });
        }
        return FString::Format(TEXT("if (!{0})\n{\n{0} = true;\n{1}\n}\n"), { StateName, Branch });
    }
    return FString();
}
