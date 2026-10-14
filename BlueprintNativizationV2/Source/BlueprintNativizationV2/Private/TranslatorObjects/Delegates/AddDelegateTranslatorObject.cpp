/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/AddDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


#include "DelegateExpressions.h"

FString UAddDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    const FGenerateResultStruct Result = GenerateMulticastDelegateStatement(Node, MacroStack, NativizationV2Subsystem);
    FString Content = GenerateNewPreparations(Preparations, Result.Preparations);
    Preparations.Append(Result.Preparations);
    return Content + Result.Code;
}