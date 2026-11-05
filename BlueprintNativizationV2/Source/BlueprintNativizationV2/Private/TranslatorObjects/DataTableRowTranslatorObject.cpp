/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "TranslatorObjects/DataTableRowTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "EdGraphSchema_K2.h"

static FString GetTableRowDeclaration(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    UEdGraphPin* Output = CastChecked<UK2Node_GetDataTableRow>(Node)->GetResultPin();
    return UBlueprintNativizationLibrary::GetPinType(Output->PinType, false) + TEXT(" ") +
        UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes) + TEXT("{};\n");
}

FString UDataTableRowTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (!FGenerationGraphContext::IsFunctionGraph(Node))
    {
        return TEXT("UPROPERTY(Transient)\n") + GetTableRowDeclaration(Node, Subsystem);
    }
    return {};
}

TSet<FString> UDataTableRowTranslatorObject::GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(Node))
    {
        return {GetTableRowDeclaration(Node, Subsystem)};
    }
    return {};
}

FGenerateResultStruct UDataTableRowTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return UBlueprintNativizationLibrary::GetUniquePinName(CastChecked<UK2Node_GetDataTableRow>(Node)->GetResultPin(), Subsystem->EntryNodes);
}

FString UDataTableRowTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_GetDataTableRow* Lookup = CastChecked<UK2Node_GetDataTableRow>(Node);
    UScriptStruct* Structure = Cast<UScriptStruct>(Lookup->GetResultPin()->PinType.PinSubCategoryObject.Get());
    if (!Structure)
    {
        FNativeApiCatalog::AddError(TEXT("Data table row lookup requires a resolved row structure: ") + Node->GetPathName());
        return {};
    }
    const FGenerateResultStruct Table = Subsystem->GenerateInputParameterCodeForNode(Node, Lookup->GetDataTablePin(), 0, MacroStack);
    const FGenerateResultStruct RowName = Subsystem->GenerateInputParameterCodeForNode(Node, Lookup->GetRowNamePin(), 0, MacroStack);
    FGenerationPreparations Required = Table.Preparations;
    Required.Append(RowName.Preparations);
    FString Content = GenerateNewPreparations(Preparations, Required);
    Preparations.Append(Required);
    const FString Output = UBlueprintNativizationLibrary::GetUniquePinName(Lookup->GetResultPin(), Subsystem->EntryNodes);
    const FString TableName = Subsystem->GetNodeStateName(Node, TEXT("DataTable"), true);
    Content += TEXT("{\nconst UDataTable* ") + TableName + TEXT(" = ") + Table.Code + TEXT(";\n");
    if (FRetainedBlueprintGeneration::IsExternalStructure(Structure))
    {
        const FString StructureName = Subsystem->GetNodeStateName(Node, TEXT("RowStructure"), true);
        const FString RowMemory = Subsystem->GetNodeStateName(Node, TEXT("RowMemory"), true);
        Content += TEXT("UScriptStruct* ") + StructureName + TEXT(" = ") +
            FRetainedBlueprintGeneration::GetAssetValue(Structure, UScriptStruct::StaticClass()) + TEXT(";\nconst uint8* ") + RowMemory +
            TEXT(" = nullptr;\nif (") + TableName + TEXT(" && ") + StructureName + TEXT(" && ") + TableName + TEXT("->GetRowStruct() == ") + StructureName + TEXT(")\n{\n") +
            RowMemory + TEXT(" = ") + TableName + TEXT("->FindRowUnchecked(") + RowName.Code + TEXT(");\n}\nif (") + RowMemory + TEXT(")\n{\n") +
            Output + TEXT(".InitializeAs(") + StructureName + TEXT(", ") + RowMemory + TEXT(");\n");
    }
    else
    {
        Content += TEXT("if (") + TableName + TEXT(" && ") + TableName + TEXT("->GetRowStruct() == ") +
            UBlueprintNativizationLibrary::GetUniqueFieldName(Structure) + TEXT("::StaticStruct() && UDataTableFunctionLibrary::Generic_GetDataTableRowFromName(") +
            TableName + TEXT(", ") + RowName.Code + TEXT(", &") + Output + TEXT("))\n{\n");
    }
    TArray<UK2Node*> FoundPath = MacroStack;
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, FoundPath, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, FoundPath);
    }
    Content += TEXT("}\nelse\n{\n");
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Lookup->GetRowNotFoundPin(), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content + TEXT("}\n}\n");
}

TSet<FString> UDataTableRowTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Kismet/DataTableFunctionLibrary.h\""));
    Includes.Add(TEXT("#include \"Engine/DataTable.h\""));
    return Includes;
}
