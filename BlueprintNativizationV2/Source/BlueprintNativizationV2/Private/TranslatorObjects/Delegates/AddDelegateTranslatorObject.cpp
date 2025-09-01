/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/AddDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


FString UAddDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_AddDelegate* AddDelegate = Cast<UK2Node_AddDelegate>(Node))
    {
        FMulticastDelegateProperty* DelegateProperty = AddDelegate->DelegateReference.ResolveMember<FMulticastDelegateProperty>(AddDelegate->GetBlueprintClassFromNode());
        if (DelegateProperty)
        {
            TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::DelegatePin);
            return UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProperty, NativizationV2Subsystem->EntryNodes) +
                "->AddDynamic(" + NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack) + ");";
        }
    }
    return FString();
}
