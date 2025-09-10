/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/AddDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


FString UAddDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_AddDelegate* AddDelegate = Cast<UK2Node_AddDelegate>(Node))
    {
        FMulticastDelegateProperty* DelegateProperty = AddDelegate->DelegateReference.ResolveMember<FMulticastDelegateProperty>(AddDelegate->GetBlueprintClassFromNode());
        if (DelegateProperty)
        {
            TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::DelegatePin);
            FGenerateResultStruct GenerateResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);

            FString Content;
			Content += GenerateNewPreparations(Preparations, GenerateResultStruct.Preparations);
			Preparations.Append(GenerateResultStruct.Preparations);

            Content += UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProperty) +
                "->AddDynamic(" + GenerateResultStruct.Code + ");";
            return Content;
        }
    }
    return FString();
}
