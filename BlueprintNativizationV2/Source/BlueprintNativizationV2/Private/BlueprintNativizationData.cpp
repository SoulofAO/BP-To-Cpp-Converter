/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationData.h"
#include "BlueprintNativizationV2.h"
#include "AutomationBlueprintFunctionLibrary.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorModeManager.h"
#include "ToolMenuSection.h"
#include "BlueprintActionMenuUtils.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "K2Node_ComponentBoundEvent.h"

FString UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(FString& Input)
{
    FString Output;
    int32 IndentLevel = 0;
    TMap<int32, FString> AccessSections;
    bool bPreviousBlank = false;
    TArray<FString> Lines;
    Input.ParseIntoArrayLines(Lines, false);
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.IsEmpty())
        {
            bPreviousBlank = true;
            continue;
        }
        if (bPreviousBlank && !Output.IsEmpty() && !Line.StartsWith(TEXT("}")) && !Output.EndsWith(TEXT("{\n")))
        {
            Output += TEXT("\n");
        }
        bPreviousBlank = false;
        int32 OpenBraces = 0;
        int32 CloseBraces = 0;
        TCHAR Quote = 0;
        for (int32 Position = 0; Position < Line.Len(); ++Position)
        {
            const TCHAR Character = Line[Position];
            if (Quote != 0)
            {
                if (Character == TEXT('\\'))
                {
                    ++Position;
                }
                else if (Character == Quote)
                {
                    Quote = 0;
                }
                continue;
            }
            if (Character == TEXT('"') || Character == TEXT('\''))
            {
                Quote = Character;
            }
            else if (Character == TEXT('/') && Position + 1 < Line.Len() && Line[Position + 1] == TEXT('/'))
            {
                break;
            }
            else if (Character == TEXT('{'))
            {
                ++OpenBraces;
            }
            else if (Character == TEXT('}'))
            {
                ++CloseBraces;
            }
        }
        if (CloseBraces > OpenBraces)
        {
            const int32 PreviousIndent = IndentLevel;
            IndentLevel = FMath::Max(0, IndentLevel - CloseBraces + OpenBraces);
            for (int32 ScopeDepth = IndentLevel + 1; ScopeDepth <= PreviousIndent; ++ScopeDepth)
            {
                AccessSections.Remove(ScopeDepth);
            }
        }
        if (Line == TEXT("public:") || Line == TEXT("protected:") || Line == TEXT("private:"))
        {
            if (AccessSections.FindRef(IndentLevel) != Line)
            {
                AccessSections.Add(IndentLevel, Line);
                Output += Line + TEXT("\n");
            }
            continue;
        }
        Output += FString::ChrN(IndentLevel * 3, TEXT(' ')) + Line + TEXT("\n");
        if (OpenBraces > CloseBraces)
        {
            IndentLevel += OpenBraces - CloseBraces;
        }
    }
    return Output;
}

FString UBlueprintNativizationDataLibrary::CutBeforeAndIncluding(const FString& Source, const FString& ToFind)
{
	int32 Index = Source.Find(ToFind);
	if (Index != INDEX_NONE)
	{
		return Source.RightChop(Index + ToFind.Len());
	}
	return Source;
}

bool UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByName(const TArray<FGenerateFunctionStruct>& GenerateFunctionStructs, FName Name, FGenerateFunctionStruct& OutGenerateFunctionStruct)
{
	for (const FGenerateFunctionStruct& Struct : GenerateFunctionStructs)
	{
		if (Struct.Name == Name)
		{
			OutGenerateFunctionStruct = Struct;
			return true;
		}
	}

	return false;
}

bool UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(const TArray<FGenerateFunctionStruct>& GenerateFunctionStructs, UK2Node* Node, FGenerateFunctionStruct& OutGenerateFunctionStruct)
{
	for (const FGenerateFunctionStruct& Struct : GenerateFunctionStructs)
	{
		if (Struct.Node == Node)
		{
			OutGenerateFunctionStruct = Struct;
			return true;
		}
	}

	return false;
}
