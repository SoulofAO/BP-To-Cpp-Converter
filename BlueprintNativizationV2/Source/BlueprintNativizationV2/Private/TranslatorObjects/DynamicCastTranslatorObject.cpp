/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/DynamicCastTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_ClassDynamicCast.h"

TSet<FString> UDynamicCastTranslatorObject::GenerateLocalVariables(UK2Node* Node,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Declarations = Super::GenerateLocalVariables(Node, MacroStack, Subsystem);
    UK2Node_DynamicCast* CastNode = CastChecked<UK2Node_DynamicCast>(Node);
    if (!CastNode->IsA<UK2Node_ClassDynamicCast>() && CastNode->TargetType->HasAnyClassFlags(CLASS_Const))
    {
        UEdGraphPin* ResultPin = CastNode->GetCastResultPin();
        const FString Declaration = UBlueprintNativizationLibrary::GetPinType(ResultPin->PinType, false) + TEXT(" ") +
            UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, Subsystem->EntryNodes) + TEXT(" = ");
        TSet<FString> QualifiedDeclarations;
        for (const FString& Statement : Declarations)
        {
            if (Statement.StartsWith(Declaration))
            {
                QualifiedDeclarations.Add(TEXT("const ") + Statement);
            }
            else
            {
                QualifiedDeclarations.Add(Statement);
            }
        }
        return QualifiedDeclarations;
    }
    return Declarations;
}

FGenerateResultStruct UDynamicCastTranslatorObject::GenerateCastExpression(UK2Node_DynamicCast* Node,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    FGenerateResultStruct Source = Subsystem->GenerateInputParameterCodeForNode(Node, Node->GetCastSourcePin(), 0, MacroStack);
    if (Source.Code == TEXT("nullptr") && !Node->TargetType->HasAnyClassFlags(CLASS_Interface))
    {
        if (Node->IsA<UK2Node_ClassDynamicCast>())
        {
            Source.Code = TEXT("static_cast<UClass*>(nullptr)");
            return Source;
        }
        Source.Code = TEXT("static_cast<") + FRetainedBlueprintGeneration::GetClassName(Node->TargetType) + TEXT("*>(nullptr)");
        return Source;
    }
    if (Node->GetCastSourcePin()->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
    {
        Source.Code = TEXT("(") + Source.Code + TEXT(").GetObject()");
    }
    const FString TargetType = FRetainedBlueprintGeneration::GetClassName(Node->TargetType);
    if (FRetainedBlueprintGeneration::IsRetained(Node->TargetType))
    {
        const FString RuntimeClass = FRetainedBlueprintGeneration::GetClassValue(Node->TargetType);
        if (Node->IsA<UK2Node_ClassDynamicCast>())
        {
            Source.Code = TEXT("[](UClass* SourceClass, UClass* TargetClass) -> UClass*\n{\nif (SourceClass && TargetClass && SourceClass->IsChildOf(TargetClass))\n{\nreturn SourceClass;\n}\nreturn nullptr;\n}(") + Source.Code + TEXT(", ") + RuntimeClass + TEXT(")");
        }
        else
        {
            Source.Code = TEXT("[](UObject* SourceObject, UClass* TargetClass) -> ") + TargetType + TEXT("*\n{\nif (IsValid(SourceObject) && TargetClass && SourceObject->IsA(TargetClass))\n{\nreturn Cast<") + TargetType + TEXT(">(SourceObject);\n}\nreturn nullptr;\n}(") + Source.Code + TEXT(", ") + RuntimeClass + TEXT(")");
        }
        return Source;
    }
    if (Node->IsA<UK2Node_ClassDynamicCast>())
    {
        Source.Code = FString::Format(TEXT("[](UClass* SourceClass) -> UClass*\n{\nif (SourceClass && SourceClass->IsChildOf({0}::StaticClass()))\n{\nreturn SourceClass;\n}\nreturn nullptr;\n}({1})"), { TargetType, Source.Code });
    }
    else if (Node->TargetType->HasAnyClassFlags(CLASS_Interface))
    {
        Source.Code = FString::Format(TEXT("TScriptInterface<{0}>({1})"), {
            UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(Node->TargetType.Get()), Source.Code });
    }
    else
    {
        Source.Code = FString::Format(TEXT("Cast<{0}>({1})"), { TargetType, Source.Code });
    }
    return Source;
}

FString UDynamicCastTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_DynamicCast* CastNode = CastChecked<UK2Node_DynamicCast>(Node);
    FGenerateResultStruct Source = GenerateCastExpression(CastNode, MacroStack, Subsystem);
    FString Content = GenerateNewPreparations(Preparations, Source.Preparations);
    Preparations.Append(Source.Preparations);
    UEdGraphPin* ResultPin = CastNode->GetCastResultPin();
    FString ResultName = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, Subsystem->EntryNodes);
    FString Declaration;
    if (ResultPin->LinkedTo.IsEmpty())
    {
        ResultName = Subsystem->GetNodeStateName(Node, TEXT("CastResult"));
        Declaration = TEXT("const auto ");
    }
    Content += Declaration + ResultName + TEXT(" = ") + Source.Code + TEXT(";\n");
    FString SuccessValue = ResultName;
    if (CastNode->TargetType->HasAnyClassFlags(CLASS_Interface))
    {
        SuccessValue += TEXT(".GetObject()");
    }
    UEdGraphPin* SuccessPin = CastNode->GetBoolSuccessPin();
    if (SuccessPin && !SuccessPin->LinkedTo.IsEmpty())
    {
        Content += UBlueprintNativizationLibrary::GetUniquePinName(SuccessPin, Subsystem->EntryNodes) +
            TEXT(" = ") + SuccessValue + TEXT(" != nullptr;\n");
    }
    TArray<UEdGraphPin*> Branches = { CastNode->GetValidCastPin(), CastNode->GetInvalidCastPin() };
    UEdGraphPin* CommonTail = Subsystem->FindCommonExecutionTail(Branches[0], Branches[1]);
    TSet<UK2Node*> DeferredNodes = Subsystem->DeferredExecutionNodes;
    if (CommonTail)
    {
        DeferredNodes.Add(CastChecked<UK2Node>(CommonTail->GetOwningNode()));
    }
    {
    TGuardValue<TSet<UK2Node*>> DeferredScope(Subsystem->DeferredExecutionNodes, MoveTemp(DeferredNodes));
    bool bGeneratedSuccess = false;
    for (int32 BranchIndex = 0; BranchIndex < Branches.Num(); ++BranchIndex)
    {
        UEdGraphPin* Branch = Branches[BranchIndex];
        if (Branch->LinkedTo.IsEmpty())
        {
            continue;
        }
        FString Condition = SuccessValue;
        if (BranchIndex == 1)
        {
            Condition = TEXT("!") + SuccessValue;
        }
        FGenerationPreparations BranchPreparations = Preparations;
        UEdGraphPin* Target = Branch->LinkedTo[0];
        const FString BranchCode = Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()),
            Target->GetName(), VisitedNodes, BranchPreparations, MacroStack);
        if (BranchCode.IsEmpty())
        {
            continue;
        }
        FString Prefix = FString::Format(TEXT("if ({0})\n"), { Condition });
        if (BranchIndex == 1 && bGeneratedSuccess)
        {
            Prefix = TEXT("else\n");
        }
        Content += Prefix + FString::Format(TEXT("{\n{0}\n}\n"), {
            BranchCode });
        if (BranchIndex == 0)
        {
            bGeneratedSuccess = true;
        }
    }
    }
    if (CommonTail)
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(CommonTail->GetOwningNode()),
            CommonTail->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}

FGenerateResultStruct UDynamicCastTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_DynamicCast* CastNode = CastChecked<UK2Node_DynamicCast>(Node);
    if (!CastNode->IsNodePure())
    {
        return UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
    }
    FGenerateResultStruct Result;
    if (const FGenerateResultStruct* Cached = Subsystem->PureFunctionCalls.Find(Node))
    {
        Result = *Cached;
    }
    else
    {
        Result = GenerateCastExpression(CastNode, MacroStack, Subsystem);
        FString ResultName = Subsystem->GetNodeStateName(Node, TEXT("CastResult"));
        if (Subsystem->DataConsumer)
        {
            ResultName = Subsystem->GetNodeStateName(Subsystem->DataConsumer, Subsystem->InputEvaluationPurpose + ResultName);
        }
        Result.Preparations.Add(FString::Format(TEXT("const auto {0} = {1};\n"), { ResultName, Result.Code }));
        Result.Code = ResultName;
        Subsystem->PureFunctionCalls.Add(Node, Result);
    }
    if (Pin == CastNode->GetBoolSuccessPin())
    {
        if (CastNode->TargetType->HasAnyClassFlags(CLASS_Interface))
        {
            Result.Code += TEXT(".GetObject()");
        }
        Result.Code = TEXT("(") + Result.Code + TEXT(" != nullptr)");
    }
    return Result;
}

TSet<FString> UDynamicCastTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes;
    UK2Node_DynamicCast* CastNode = CastChecked<UK2Node_DynamicCast>(Node);
    FString Include;
    if (Subsystem->GetIncludeFromField(CastNode->TargetType, Include))
    {
        Includes.Add(Include);
    }
    if (UClass* SourceClass = Cast<UClass>(CastNode->GetCastSourcePin()->DefaultObject))
    {
        if (Subsystem->GetIncludeFromField(SourceClass, Include))
        {
            Includes.Add(Include);
        }
    }
    return Includes;
}
