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

	TArray<FString> Lines;
	Input.ParseIntoArrayLines(Lines, false);

	for (FString& Line : Lines)
	{
		Line = Line.TrimStartAndEnd();

		if (Line == TEXT("public:") || Line == TEXT("protected:") || Line == TEXT("private:"))
		{
			Output += Line + TEXT("\n");
			continue;
		}

		// ������� ���������� ����������� � ����������� ������ � ������
		int32 OpenBraces = 0;
		int32 CloseBraces = 0;

		for (int32 i = 0; i < Line.Len(); ++i)
		{
			if (Line[i] == '{') ++OpenBraces;
			else if (Line[i] == '}') ++CloseBraces;
		}

		// ��������� ������� ������� �� ������ ������, ���� ������ �������� ����������� ������
		// ��� ���������������� ���������� �����������
		if (CloseBraces > OpenBraces)
		{
			IndentLevel -= (CloseBraces - OpenBraces);
			IndentLevel = FMath::Max(IndentLevel, 0);
		}

		FString AppendSpace;
		for (int x = 0; x < IndentLevel; x++)
		{
			AppendSpace += TEXT("   ");
		}
		Output += AppendSpace + Line + TEXT("\n");

		// ����������� ������� ������� ����� ������
		if (OpenBraces > CloseBraces)
		{
			IndentLevel += (OpenBraces - CloseBraces);
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
