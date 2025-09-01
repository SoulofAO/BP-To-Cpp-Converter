/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "BlueprintNativizationData.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BlueprintNativizationUniqueNameSubsystem.generated.h"

class UK2Node;
class UEdGraphPin;


struct FNodeNamePair
{
public:
	UK2Node* Node = nullptr;
	FName Name;

	bool operator==(const FNodeNamePair& Other) const
	{
		return Node == Other.Node && Name == Other.Name;
	}

	bool operator==(UK2Node* OtherNode) const
	{
		return Node == OtherNode;
	}
};

struct FPropertyNamePair
{
public:
	FProperty* Property = nullptr;
	UEdGraphPin* GraphPin = nullptr;
	FName Name;
	UK2Node* LambdaNode = nullptr;
	FString LambdaMainName;

	FPropertyNamePair(FProperty* NewProperty, UEdGraphPin* NewGraphPin, FName NewName, UK2Node* NewLambdaNode, FString NewLambdaMainName)
	{
		Property = NewProperty;
		GraphPin = NewGraphPin;
		Name = NewName;
		LambdaNode = NewLambdaNode;
		LambdaMainName = NewLambdaMainName;
	}


	bool operator==(const FPropertyNamePair& Other) const
	{
		return Property == Other.Property && Name == Other.Name;
	}

	bool operator==(FProperty* OtherProperty) const
	{
		return Property == OtherProperty;
	}

	bool operator==(UEdGraphPin* EdGraphPin) const
	{
		return GraphPin == EdGraphPin;
	}
};


struct FRegisterNameField
{
public:
	FName Name;
	UField* Field = nullptr;

	FRegisterNameField(FName NewName, UField* NewField)
	{
		Name = NewName;
		Field = NewField;
	}
	bool operator==(UField* OtherField) const
	{
		return Field == OtherField;
	}
};

struct FRegisterNameFunction
{
public:
	FRegisterNameFunction(TSubclassOf<UObject> NewClass)
	{
		Class = NewClass;
	}
	UClass* Class = nullptr;
	TMap<FName, int> UniqueFunctionCounter;

	TArray<FNodeNamePair> NodeNamePairs;

	bool operator==(const UClass* Other) const
	{
		return Other == Class;
	}
};

struct FRegisterVariable
{

public:
	FRegisterVariable(UClass* NewClass, UFunction* NewFunction, UK2Node* NewGeneratedFunctionEntryNode, bool NewIsConstructorVar)
	{
		Class = NewClass;
		Function = NewFunction;
		GeneratedFunctionEntryNode = NewGeneratedFunctionEntryNode;
		IsConstructorVar = NewIsConstructorVar;
	}

	UClass* Class;
	UFunction* Function = nullptr;
	UK2Node* GeneratedFunctionEntryNode = nullptr;
	bool IsConstructorVar = true;

	TMap<FName, int> Counters;

	TArray<FPropertyNamePair> PropertyNamePairs;

	bool operator==(const FRegisterVariable& OtherClass) const
	{
		return OtherClass.Function == Function && OtherClass.Class == Class && OtherClass.GeneratedFunctionEntryNode == GeneratedFunctionEntryNode && IsConstructorVar == OtherClass.IsConstructorVar;
	}

};

UCLASS()
class UBlueprintNativizationUniqueNameSubsystem: public UEngineSubsystem
{
	GENERATED_BODY()

public:
	FString SanitizeIdentifier(const FString& Input);

	FString GetDefaultBaseName(UField* Field);

	FName GetUniqueFieldName(UField* Field);

	TArray<FName> GetAllFieldNames();

	TArray<FName> GetAllFunctionNames(UClass* Class);

	TArray<FName> GetAllClassGlobalPropertyNames(UClass* Class);

	FName GetUniqueFunctionName_Internal(UK2Node* EntryNode, FString BaseFunctionName);

	FName GetUniqueFunctionName(UFunction* Function, FString PredictName);

	FName GetUniqueFunctionName(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString PredictName);

	FName GetUniqueVariableName(FProperty* Property);

	//Lambda Var, its for the local generated variable in function, but use only PER ONE NODE.
	// For example, classic delay node can transphere in C++ sistem only with use FTimerHandle.
	// This variable get his unique name by use Lambda. 
	FName GetLambdaUniqueVariableName(FString LambdaMainName, UK2Node* LambdaNode, TArray<FGenerateFunctionStruct> EntryNodes);

	FName GetLambdaUniqueVariableName(FString LambdaMainName, UFunction* Function, TArray<FGenerateFunctionStruct> EntryNodes);

	FName GetLambdaUniqueVariableName(FString LambdaMainName, UClass* OwningClass);

	FName GenerateUniqueLambdaVariableName(const FString& LambdaMainName, UClass* OwningClass, UFunction* OwningFunction, UK2Node* LambdaNode, UK2Node* EntryNode);

	FName GenerateConstructorLambdaUniqueVariableName(FString LambdaMainName, UClass* Class);

	FName GetUniquePinVariableName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes);

	bool CheckUnquieVarName(FString CheckName, UClass* Class);

	void Reset();

	//Real Name - counter
	TMap<FName, int> UniqueFieldCounter;

	TArray<FRegisterNameField> UniqueFieldNameArray;

	TArray<FRegisterNameFunction> UniqueFunctionNameArray;

	TArray<FRegisterVariable> UniqueVarNamesArray;
};
