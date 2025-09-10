/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/RemoveAllDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString URemoveAllDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_ClearDelegate* RemoveDelegate = Cast<UK2Node_ClearDelegate>(Node))
    {
        FMulticastDelegateProperty* DelegateProperty = RemoveDelegate->DelegateReference.ResolveMember<FMulticastDelegateProperty>(RemoveDelegate->GetBlueprintClassFromNode());
        if (DelegateProperty)
        {
            TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::DelegatePin);
            return UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProperty) + TEXT("->Clear();");
        }
    }
    return FString();
}
