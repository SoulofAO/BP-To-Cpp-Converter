/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/CallDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UCallDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_CallDelegate* CallDelegate = Cast<UK2Node_CallDelegate>(Node))
    {
        FMulticastDelegateProperty* DelegateProperty = CallDelegate->DelegateReference.ResolveMember<FMulticastDelegateProperty>(CallDelegate->GetBlueprintClassFromNode());
        if (DelegateProperty)
        {
            UFunction* SignatureFunction = DelegateProperty->SignatureFunction;
            
            TArray<FString> ParametersInput;
            for (TFieldIterator<FProperty> ParamIt(SignatureFunction); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
            {
                ParametersInput.Add(NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, CallDelegate->FindPin(ParamIt->GetName()), 0, MacroStack));
            }
            return FString::Format(TEXT("{0}->Broadcast({1})"), { UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProperty, NativizationV2Subsystem->EntryNodes), 
                FString::Join(ParametersInput, TEXT(","))});
        }
    }
    return FString();
}
