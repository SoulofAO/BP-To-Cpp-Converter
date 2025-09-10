/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BlueprintNativizationSettings.h"
#include "K2Node_EnhancedInputAction.h"
#include "GameFramework/TouchInterface.h"

FString UBlueprintNativizationUniqueNameSubsystem::SanitizeIdentifier(const FString& Input)
{
	FString Result;

	for (TCHAR Char : Input)
	{
		if (FChar::IsAlpha(Char) || FChar::IsDigit(Char) || Char == TEXT('_'))
		{
			Result.AppendChar(Char);
		}
	}

	if (!Result.IsEmpty() && FChar::IsDigit(Result[0]))
	{
		Result = TEXT("_") + Result;
	}

	if (Result.IsEmpty())
	{
		return TEXT("_invalid");
	}

	return Result;
}
FString UBlueprintNativizationUniqueNameSubsystem::GetDefaultBaseName(UField* Field)
{
	auto HasValidPrefix = [](const FString& Name, const FString& Prefix)
		{
			return Name.StartsWith(Prefix)
				&& Name.Len() > Prefix.Len()
				&& FChar::IsUpper(Name[Prefix.Len()]);
		};

	if (!Field)
	{
		return FString();
	}

	FString Name = Field->GetName();

	Name = UBlueprintNativizationLibrary::RemoveBPPrefix(Name);

	if (UClass* Class = Cast<UClass>(Field)) 
	{
		if (Class->IsChildOf<UInterface>() || Class->HasAnyClassFlags(CLASS_Interface))
		{
			if (HasValidPrefix(Name, TEXT("I_")))
			{
				Name = Name.Mid(2); 
			}
			else if (HasValidPrefix(Name, TEXT("I")))
			{
				Name = Name.Mid(1);
			}
			else if(HasValidPrefix(Name, TEXT("U")))
			{
				Name = Name.Mid(1);
			}
			else if(HasValidPrefix(Name, TEXT("U_")))
			{
				Name = Name.Mid(2);
			}
		}
	}

	if (const UEnum* Enum = Cast<UEnum>(Field))
	{
		Name = HasValidPrefix(Name, TEXT("E")) ? Name : TEXT("E") + Name;
	}
	else if (UClass* Class = Cast<UClass>(Field))
	{
		if (Class->IsChildOf<AActor>() || Class == AActor::StaticClass())
		{
			Name = HasValidPrefix(Name, TEXT("A")) ? Name : TEXT("A") + Name;
		}
		else if (Class->IsChildOf<UInterface>() || Class->HasAnyClassFlags(CLASS_Interface))
		{
		}
		else if (Field->IsA<UObject>())
		{
			Name = HasValidPrefix(Name, TEXT("U")) ? Name : TEXT("U") + Name;
		}
	}
	else if (Field->IsA<UStruct>())
	{
		Name = HasValidPrefix(Name, TEXT("F")) ? Name : TEXT("F") + Name;
	}

	Name = Name.Replace(TEXT(" "), TEXT("_"));

	return Name;
}


FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueFieldName(UField* Field)
{
	if (!Field)
	{
		return NAME_None;
	}

	FString BaseName = GetDefaultBaseName(Field);
	
	BaseName = SanitizeIdentifier(BaseName);
	if (UBlueprintNativizationLibrary::IsCppDefinedField(Field))
	{
		return FName(*BaseName);
	}

	FRegisterNameField* RegisterNameField = UniqueFieldNameArray.FindByKey(Field);
	if (RegisterNameField)
	{
		return RegisterNameField->Name;
	}

	if (BaseName.EndsWith(TEXT("_C")))
	{
		BaseName.LeftChopInline(2);
	}

	int32& Counter = UniqueFieldCounter.FindOrAdd(*BaseName);

	FString NewNameString = BaseName; 
	while (true)
	{
		NewNameString = NewNameString + ((Counter == 0) ? "" : FString::FromInt(Counter));
		Counter++;
		if (Cast<UClass>(Field) && (Cast<UClass>(Field)->IsChildOf<UInterface>() || Cast<UClass>(Field)->HasAnyClassFlags(CLASS_Interface)))
		{
			if (!GetAllFieldNames().Contains(FName(*(TEXT("U") + NewNameString))) && !GetAllFieldNames().Contains(FName(*(TEXT("I") + NewNameString))))
			{
				break;
			}
		}
		else if (!GetAllFieldNames().Contains(FName(*NewNameString)))
		{
			break;
		}
	}

	UniqueFieldNameArray.Add(FRegisterNameField(*NewNameString, Field));

	return *NewNameString;
}

TArray<FName> UBlueprintNativizationUniqueNameSubsystem::GetAllFieldNames()
{
	TArray<FName> Names;

	for (TObjectIterator<UField> FieldIt; FieldIt; ++FieldIt)
	{
		UField* Field = *FieldIt;

		if (UBlueprintNativizationLibrary::IsCppDefinedField(Field))
		{
			Names.Add(*GetDefaultBaseName(Field));
		}
	}

	for (FRegisterNameField RegisterNameField : UniqueFieldNameArray)
	{
		Names.Add(RegisterNameField.Name);
	}

	return Names;
}

TArray<FName> UBlueprintNativizationUniqueNameSubsystem::GetAllFunctionNames(UClass* Class)
{
	TArray<FName> Names;

	if (!Class)
	{
		return Names;
	}

	for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
	{
		UFunction* Function = *FuncIt;
		if (!Function)
		{
			continue;
		}

		if (!Function->GetOwnerClass() || !Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
		{
			continue;
		}

		Names.Add(Function->GetFName());
	}

	FRegisterNameFunction* RegisterNameFunction = UniqueFunctionNameArray.FindByKey(Class);

	if (!RegisterNameFunction)
	{
		return Names;
	}

	for (FNodeNamePair NodeNamePair : RegisterNameFunction->NodeNamePairs)
	{
		Names.Add(NodeNamePair.Name);
	}

	return Names;
}



TArray<FName> UBlueprintNativizationUniqueNameSubsystem::GetAllClassGlobalPropertyNames(UClass* Class)
{
	TArray<FName> Names;

	if (!Class)
	{
		return Names;
	}

	for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property)
		{
			continue;
		}

		UStruct* OwnerStruct = Property->GetOwnerStruct();
		if (!OwnerStruct || !OwnerStruct->IsNative())
		{
			continue;
		}

		Names.Add(Property->GetFName());
	}

	FRegisterVariable* RegisterVariable = UniqueVarNamesArray.FindByKey(FRegisterVariable(Class, nullptr, nullptr, false));

	if (!RegisterVariable)
	{
		return Names;
	}

	for (FPropertyNamePair PropertyNamePair : RegisterVariable->PropertyNamePairs)
	{
		Names.Add(PropertyNamePair.Name);
	}

	return Names;
}


FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueFunctionName_Internal(UK2Node* EntryNode, FString BaseFunctionName)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	if (!EntryNode || !EntryNode->GetBlueprint())
	{
		return NAME_None;
	}

	TSubclassOf<UObject> Class = EntryNode->GetBlueprint()->GeneratedClass;
	if (!Class)
	{
		return NAME_None;
	}

	FRegisterNameFunction* ExistingFunctionEntry = UniqueFunctionNameArray.FindByKey(Class);
	if (!ExistingFunctionEntry)
	{
		FRegisterNameFunction NewEntry(Class);
		UniqueFunctionNameArray.Add(NewEntry);
		ExistingFunctionEntry = UniqueFunctionNameArray.FindByKey(Class);
	}

	if (FNodeNamePair* ExistingPair = ExistingFunctionEntry->NodeNamePairs.FindByKey(EntryNode))
	{
		return ExistingPair->Name;
	}

	BaseFunctionName = SanitizeIdentifier(BaseFunctionName);
	int& Counter = ExistingFunctionEntry->UniqueFunctionCounter.FindOrAdd(*BaseFunctionName);

	FString UniqueNameString;
	while (true)
	{
		if (Cast<UK2Node_FunctionEntry>(EntryNode) || Cast<UK2Node_Event>(EntryNode) || Cast<UK2Node_EnhancedInputAction>(EntryNode))
		{
			UniqueNameString = FString::Format(TEXT("{0}{1}"), { *BaseFunctionName, (Counter == 0 ? TEXT("") : *FString::FromInt(Counter)) });
		}
		else
		{
			if (Settings->bEnableGenerateSuffix)
			{
				UniqueNameString = FString::Format(TEXT("{0}_GenerateFunction{1}"), { *BaseFunctionName, (Counter == 0 ? TEXT("") : *FString::FromInt(Counter)) });

			}
			else
			{
				UniqueNameString = FString::Format(TEXT("{0}{1}"), { *BaseFunctionName, (Counter == 0 ? TEXT("") : *FString::FromInt(Counter)) });
			}
		}
		Counter++;

		if (!GetAllFieldNames().Contains(FName(*UniqueNameString)))
		{
			break;
		}
	}

	FName UniqueFunctionName(*UniqueNameString);
	ExistingFunctionEntry->NodeNamePairs.Add({ EntryNode, UniqueFunctionName });

	return UniqueFunctionName;
}

FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueFunctionName(UFunction* Function, FString PredictName)
{
	if (Function && Function->IsNative())
	{
		return *SanitizeIdentifier(Function->GetName());
	}

	UK2Node* EntryNode = Cast<UK2Node>(
		UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(
			UBlueprintNativizationLibrary::GetBlueprintFromFunction(Function),
			Function->GetFName())
	);

	FName OriginalName = UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(EntryNode);
	const FString BaseFunctionName = OriginalName.IsNone() ? PredictName : OriginalName.ToString();
	return GetUniqueFunctionName_Internal(EntryNode, BaseFunctionName);
}

FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueFunctionName(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString PredictName)
{
	FName OriginalName = UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(EntryNode);
	const FString BaseFunctionName = OriginalName.IsNone() ? PredictName : OriginalName.ToString();
	return GetUniqueFunctionName_Internal(EntryNode, BaseFunctionName);
}

FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueVariableName(FProperty* Property)
{
	if (!Property)
	{
		return NAME_None;
	}

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	UClass* OwnerClass = Property->GetOwnerClass();
	if (OwnerClass)
	{
		UObject* Object = Property->GetOwnerUObject();
		if (OwnerClass && OwnerClass->GetName().StartsWith(TEXT("SKEL_")))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
			{
				if (UClass* Generated = BP->GeneratedClass)
				{
					OwnerClass = Generated;
					UFunction* Function = Cast<UFunction>(Property->GetOwnerUObject());

					if (Function)
					{
						Property = Function->FindPropertyByName(Property->GetFName());
					}
					else
					{
						Property = OwnerClass->FindPropertyByName(Property->GetFName());
					}
				}
			}
		}
		if (!Property ||!OwnerClass)
		{
			return NAME_None;
		}
	}
	
	if (UBlueprintNativizationLibrary::IsCppDefinedProperty(Property))
	{
		/*
		FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
		if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByPropertyName(Property, OutGetterAndSetterDescriptorDesc))
		{
			return *(FString::Format(TEXT("{0}()"), { OutGetterAndSetterDescriptorDesc.GetterPropertyFunctionName }));
		}*/

		return *Property->GetNameCPP();
	}

	UFunction* OwningFunction = nullptr;

	if (Property->GetOwner<UFunction>())
	{
		OwningFunction = Property->GetOwner<UFunction>();
	}


	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwnerClass, OwningFunction, nullptr, false));

	if (ExistingRegister)
	{
		FPropertyNamePair* ExistingPair = ExistingRegister->PropertyNamePairs.FindByKey(Property);
		if (ExistingPair)
		{
			return ExistingPair->Name;
		}
	}
	else
	{
		FRegisterVariable NewRegister(OwnerClass, OwningFunction, nullptr, false);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister =UniqueVarNamesArray.FindByKey(NewRegister);
	}

	FString BasePropertyName = Property->GetDisplayNameText().ToString();
	BasePropertyName = SanitizeIdentifier(BasePropertyName);

	FString UniqueNameString;

	if (!ExistingRegister->Counters.Contains(*BasePropertyName))
	{
		ExistingRegister->Counters.Add(*BasePropertyName, 0);
	}

	int& Counter = ExistingRegister->Counters.FindOrAdd(*BasePropertyName);
	
	while (true)
	{
		UniqueNameString = FString::Format(TEXT("{0}{1}"), { *BasePropertyName, ((Counter == 0) ? "" : FString::FromInt(Counter)) });
		Counter++;

		if (CheckUnquieVarName(UniqueNameString, OwnerClass))
		{
			break;
		}
	}


	ExistingRegister->PropertyNamePairs.Add(FPropertyNamePair(Property, nullptr, *UniqueNameString,nullptr, ""));

	return *UniqueNameString;
}
FName UBlueprintNativizationUniqueNameSubsystem::GetLambdaUniqueVariableName(FString LambdaMainName, UK2Node* LambdaNode, TArray<FGenerateFunctionStruct> EntryNodes)
{
	UClass* OwningClass = LambdaNode->GetBlueprint()->GeneratedClass;
	UFunction* OwningFunction = UBlueprintNativizationLibrary::FindFunctionByFunctionName(
		LambdaNode->GetBlueprint(),
		*UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(LambdaNode)
	);

	FGenerateFunctionStruct EntryNodeStruct;
	UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(LambdaNode, EntryNodes, EntryNodeStruct);

	return GenerateUniqueLambdaVariableName(LambdaMainName, OwningClass, OwningFunction, LambdaNode, EntryNodeStruct.Node);
}

FName UBlueprintNativizationUniqueNameSubsystem::GetLambdaUniqueVariableName(FString LambdaMainName, UFunction* Function, TArray<FGenerateFunctionStruct> EntryNodes)
{
	UClass* OwningClass = Function->GetOwnerClass();
	UK2Node* EntryNode = UBlueprintNativizationLibrary::GetEntryNodesByFunction(Function);

	return GenerateUniqueLambdaVariableName(LambdaMainName, OwningClass, Function, nullptr, EntryNode);
}

FName UBlueprintNativizationUniqueNameSubsystem::GetLambdaUniqueVariableName(FString LambdaMainName, UClass* OwningClass)
{
	return GenerateUniqueLambdaVariableName(LambdaMainName, OwningClass, nullptr, nullptr, nullptr);
}

FName UBlueprintNativizationUniqueNameSubsystem::GenerateUniqueLambdaVariableName(
	const FString& LambdaMainName,
	UClass* OwningClass,
	UFunction* OwningFunction,
	UK2Node* LambdaNode,
	UK2Node* EntryNode
)
{
	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwningClass, OwningFunction, LambdaNode, false));

	if (!ExistingRegister)
	{
		FRegisterVariable NewRegister(OwningClass, OwningFunction, EntryNode, false);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister = UniqueVarNamesArray.FindByKey(NewRegister);
	}

	for (const FPropertyNamePair& Pair : ExistingRegister->PropertyNamePairs)
	{
		if (Pair.LambdaNode == LambdaNode && Pair.LambdaMainName == LambdaMainName)
		{
			return Pair.Name;
		}
	}

	int& Counter = ExistingRegister->Counters.FindOrAdd(*LambdaMainName);
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	FString UniqueNameString;
	do
	{
		UniqueNameString = Settings->bEnableGenerateSuffix
			? FString::Format(TEXT("{0}_GeneratedValue{1}"), { *LambdaMainName, (Counter == 0 ? TEXT("") : *FString::FromInt(Counter)) })
			: FString::Format(TEXT("{0}{1}"), { *LambdaMainName, (Counter == 0 ? TEXT("") : *FString::FromInt(Counter)) });

		Counter++;
	} while (!CheckUnquieVarName(UniqueNameString, OwningClass));

	ExistingRegister->PropertyNamePairs.Add(FPropertyNamePair(nullptr, nullptr, *UniqueNameString, LambdaNode, LambdaMainName));
	return *UniqueNameString;
}


FName UBlueprintNativizationUniqueNameSubsystem::GenerateConstructorLambdaUniqueVariableName(FString LambdaMainName, UClass* Class)
{
	UClass* OwningClass = Class;

	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwningClass, nullptr, nullptr, true));

	if (!ExistingRegister)
	{
		FRegisterVariable NewRegister(OwningClass, nullptr, nullptr, true);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister = UniqueVarNamesArray.FindByKey(NewRegister);
	}

	int& Counter = ExistingRegister->Counters.FindOrAdd(*LambdaMainName);

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	FString UniqueNameString;

	while (true)
	{
		if (Settings->bEnableGenerateSuffix)
		{
			UniqueNameString = FString::Format(TEXT("{0}_GeneratedValue{1}"), { *LambdaMainName, ((Counter == 0) ? "" : FString::FromInt(Counter)) });
		}
		else
		{
			UniqueNameString = FString::Format(TEXT("{0}{1}"), { *LambdaMainName, ((Counter == 0) ? "" : FString::FromInt(Counter)) });
		}

		Counter++;

		if (CheckUnquieVarName(UniqueNameString, OwningClass))
		{
			break;
		}
	}

	ExistingRegister->PropertyNamePairs.Add(FPropertyNamePair(nullptr, nullptr, *UniqueNameString, nullptr, LambdaMainName));

	return *UniqueNameString;
}


FName UBlueprintNativizationUniqueNameSubsystem::GetUniquePinVariableName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!Pin || !Pin->GetOwningNode() || !Cast<UK2Node>(Pin->GetOwningNode())->GetBlueprint())
	{
		return NAME_None;
	}
	
	UBlueprint* Blueprint = Cast<UK2Node>(Pin->GetOwningNode())->GetBlueprint();
	UClass* OwningClass = Blueprint->GeneratedClass;

	FGenerateFunctionStruct GenerateFunctionStruct;
	if (!UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(Cast<UK2Node>(Pin->GetOwningNode()), EntryNodes, GenerateFunctionStruct))
	{
		return NAME_None;
	}
	UK2Node* EntryNode = GenerateFunctionStruct.Node;

	UFunction* OwningFunction = UBlueprintNativizationLibrary::FindFunctionByFunctionName(Blueprint, *UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(EntryNode));

	if (UK2Node_FunctionEntry* FunctionEntryNode = Cast<UK2Node_FunctionEntry>(Pin->GetOwningNode()))
	{
		return *UBlueprintNativizationLibrary::GetUniquePropertyName(OwningFunction->FindPropertyByName(*Pin->GetName()));
	}
	else if (UK2Node_Event* EventEntryNode = Cast<UK2Node_Event>(Pin->GetOwningNode()))
	{
		return *UBlueprintNativizationLibrary::GetUniquePropertyName(OwningFunction->FindPropertyByName(*Pin->GetName()));
	}

	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwningClass, OwningFunction, EntryNode, false));

	if (ExistingRegister)
	{
		FPropertyNamePair* ExistingPair = ExistingRegister->PropertyNamePairs.FindByKey(Pin);
		if (ExistingPair)
		{
			return ExistingPair->Name;
		}
	}
	else
	{
		FRegisterVariable NewRegister(OwningClass, OwningFunction, EntryNode, false);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister =UniqueVarNamesArray.FindByKey(NewRegister);
	}

	FString BasePinName = Pin->PinName.ToString();
	BasePinName = SanitizeIdentifier(BasePinName);
	int& Counter = ExistingRegister->Counters.FindOrAdd(*BasePinName);

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	FString UniqueNameString = "";
	
	while (true)
	{
		if (Settings->bEnableGenerateSuffix)
		{
			UniqueNameString = FString::Format(TEXT("{0}_GeneratedValue{1}"), { *BasePinName, ((Counter == 0) ? "" : FString::FromInt(Counter)) });
		}
		else
		{
			UniqueNameString = FString::Format(TEXT("{0}{1}"), { *BasePinName, ((Counter == 0) ? "" : FString::FromInt(Counter)) });
		}

		Counter++;

		if (CheckUnquieVarName(UniqueNameString, OwningClass))
		{
			break;
		}
	}

	ExistingRegister->PropertyNamePairs.Add(FPropertyNamePair(nullptr, Pin,  *UniqueNameString, nullptr, ""));

	return *UniqueNameString;
}

bool UBlueprintNativizationUniqueNameSubsystem::CheckUnquieVarName(FString CheckName, UClass* Class)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	TArray<FName> AllFieldsNames = GetAllFieldNames();
	AllFieldsNames.Append(GetAllFunctionNames(Class));
	AllFieldsNames.Append(GetAllClassGlobalPropertyNames(Class));
	AllFieldsNames.Append(Settings->GlobalVariableNames);
	if (!AllFieldsNames.Contains(CheckName))
	{
		return true;
	}

	return false;
}


void UBlueprintNativizationUniqueNameSubsystem::Reset()
{
	UniqueFieldCounter.Empty();
	UniqueFieldNameArray.Empty();
	UniqueFunctionNameArray.Empty();
	UniqueVarNamesArray.Empty();
}

