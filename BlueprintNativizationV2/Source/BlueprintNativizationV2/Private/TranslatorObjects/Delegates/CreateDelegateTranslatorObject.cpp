/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/CreateDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UCreateDelegateTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
    {
        UFunction* Function = Node->GetBlueprint()->FindFunction(CreateDelegateNode->GetFunctionName());
        UK2Node* EnterNode = Cast<UK2Node>(UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(Node->GetBlueprint(), CreateDelegateNode->GetFunctionName()));

        return FString::Format(TEXT("{0}, &{1}::{2}"), 
            {
                NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node,CreateDelegateNode->GetObjectInPin(), 0, TArray<UK2Node*>()), 
                UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass), 
                UBlueprintNativizationLibrary::GetUniqueEntryNodeName(EnterNode, NativizationV2Subsystem->EntryNodes, "") 
            });
    }
    return FString();
}
