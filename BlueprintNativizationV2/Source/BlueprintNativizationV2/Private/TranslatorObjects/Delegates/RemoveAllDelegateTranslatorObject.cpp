/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/RemoveAllDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

#include "DelegateExpressions.h"

FString URemoveAllDelegateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    const FGenerateResultStruct Result = GenerateMulticastDelegateStatement(Node, MacroStack, NativizationV2Subsystem);
    FString Content = GenerateNewPreparations(Preparations, Result.Preparations);
    Preparations.Append(Result.Preparations);
    return Content + Result.Code;
}