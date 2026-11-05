/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "NativeGenerationContext.h"
#include "GenerationGraphContext.h"
#include "NativeModuleCache.h"
#include "NativeApiCatalog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "BlueprintNativizationSettings.h"
#include "K2Node_EnhancedInputAction.h"
#include "GameFramework/TouchInterface.h"

FString UBlueprintNativizationUniqueNameSubsystem::SanitizeIdentifier(const FString& Input)
{
	FString Result;

	for (TCHAR Character : Input)
	{
		if ((Character >= TEXT('A') && Character <= TEXT('Z')) ||
            (Character >= TEXT('a') && Character <= TEXT('z')) ||
            (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_'))
		{
			Result.AppendChar(Character);
		}
        else if (Character > 127)
        {
            Result += TEXT("U") + FString::FromInt(static_cast<int32>(Character));
        }
	}

	if (!Result.IsEmpty() && !FChar::IsAlpha(Result[0]))
	{
		Result = TEXT("Value") + Result;
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

    if (UBlueprintNativizationLibrary::IsCppDefinedField(Field))
    {
        if (UClass* Class = Cast<UClass>(Field))
        {
            if (Class->HasAnyClassFlags(CLASS_Interface))
            {
                return Class->GetName();
            }
            return FString(Class->GetPrefixCPP()) + Class->GetName();
        }
        if (UScriptStruct* Structure = Cast<UScriptStruct>(Field))
        {
            return Structure->GetStructCPPName();
        }
        if (UEnum* Enumeration = Cast<UEnum>(Field))
        {
            return Enumeration->CppType;
        }
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


FString UBlueprintNativizationUniqueNameSubsystem::GetModuleQualifiedFieldName(UField* Field, const FString& BaseName)
{
    const FString ModuleName = SanitizeIdentifier(GetDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName);
    const UClass* Class = Cast<UClass>(Field);
    if (Class && Class->HasAnyClassFlags(CLASS_Interface))
    {
        return ModuleName + BaseName;
    }
    return BaseName.Left(1) + ModuleName + BaseName.RightChop(1);
}

static FString GetReflectedFieldName(UField* Field, FString Name)
{
    if (const UClass* Class = Cast<UClass>(Field))
    {
        if (!Class->HasAnyClassFlags(CLASS_Interface))
        {
            Name.RemoveFromStart(Class->GetPrefixCPP());
        }
    }
    else if (Field->IsA<UScriptStruct>())
    {
        Name.RemoveFromStart(TEXT("F"));
    }
    else if (!Field->IsA<UEnum>())
    {
        return FString();
    }
    return Name;
}

bool UBlueprintNativizationUniqueNameSubsystem::IsReflectedFieldNameUsed(UField* Field, const FString& Name) const
{
    const FName ReflectedName(*GetReflectedFieldName(Field, Name));
    if (ReflectedName.IsNone())
    {
        return false;
    }
    return NativeReflectedFieldNames.Contains(ReflectedName) || UniqueFieldNameArray.ContainsByPredicate(
        [Field, ReflectedName](const FRegisterNameField& Registered)
        {
            return Registered.Field != Field && FName(*GetReflectedFieldName(Registered.Field, Registered.Name.ToString())) == ReflectedName;
        });
}

FString UBlueprintNativizationUniqueNameSubsystem::GetSourceNameConflict(UField* Field, const FString& Name) const
{
    const FString ReflectedName = GetReflectedFieldName(Field, Name);
    if (!ReflectedName.IsEmpty() && NativeReflectedFieldNames.Contains(FName(*ReflectedName)))
    {
        return TEXT("Reflected type name is already used by a native type: ") + ReflectedName;
    }
    const UClass* Class = Cast<UClass>(Field);
    if (Class && Class->HasAnyClassFlags(CLASS_Interface))
    {
        if (const FString* Evidence = SourceNameConflicts.Find(TEXT("U") + Name))
        {
            return *Evidence;
        }
        if (const FString* Evidence = SourceNameConflicts.Find(TEXT("I") + Name))
        {
            return *Evidence;
        }
        return FString();
    }
    if (const FString* Evidence = SourceNameConflicts.Find(Name))
    {
        return *Evidence;
    }
    return FString();
}

FName UBlueprintNativizationUniqueNameSubsystem::GetUniqueFieldName(UField* Field)
{
    if (UClass* Class = Cast<UClass>(Field))
    {
        Field = FNativeGenerationContext::ResolveClass(Class);
    }
	if (!Field)
	{
		return NAME_None;
	}

    if (UClass* Class = Cast<UClass>(Field))
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
        {
            if (Blueprint->GeneratedClass)
            {
                Field = FNativeGenerationContext::ResolveClass(Blueprint->GeneratedClass);
            }
        }
    }
	FString BaseName = GetDefaultBaseName(Field);
	
    if (UBlueprintNativizationLibrary::IsCppDefinedField(Field))
    {
        return FName(*BaseName);
    }
    BaseName = SanitizeIdentifier(BaseName);
    if (BaseName.EndsWith(TEXT("_C")))
    {
        BaseName.LeftChopInline(2);
    }

	FRegisterNameField* RegisterNameField = UniqueFieldNameArray.FindByKey(Field);
	if (RegisterNameField)
	{
		return RegisterNameField->Name;
	}

    if (const FName* PreviousName = PreviousFieldNames.Find(UBlueprintNativizationLibrary::GetStableFieldIdentifier(Field)))
    {
        bool bPreviousNameAvailable = GetSourceNameConflict(Field, PreviousName->ToString()).IsEmpty() && !NativeFieldNames.Contains(*PreviousName) &&
            !IsReflectedFieldNameUsed(Field, PreviousName->ToString());
        if (const UClass* Class = Cast<UClass>(Field); Class && Class->HasAnyClassFlags(CLASS_Interface))
        {
            bPreviousNameAvailable = bPreviousNameAvailable && !IsFieldNameUsed(FName(*(TEXT("U") + PreviousName->ToString()))) &&
                !IsFieldNameUsed(FName(*(TEXT("I") + PreviousName->ToString())));
        }
        if (bPreviousNameAvailable && !UniqueFieldNameArray.ContainsByPredicate([PreviousName](const FRegisterNameField& RegisteredField)
        {
            return RegisteredField.Name == *PreviousName;
        }))
        {
            UniqueFieldNameArray.Add(FRegisterNameField(*PreviousName, Field));
            return *PreviousName;
        }
    }

    const FString ConflictEvidence = GetSourceNameConflict(Field, BaseName);
    if (!ConflictEvidence.IsEmpty())
	{
        BaseName = GetModuleQualifiedFieldName(Field, BaseName);
	}

	int32& Counter = UniqueFieldCounter.FindOrAdd(*BaseName);

	FString NewNameString = BaseName; 
	while (true)
	{
		NewNameString = BaseName;
        if (Counter > 0)
        {
            NewNameString += FString::FromInt(Counter);
        }
		Counter++;
        if (IsReflectedFieldNameUsed(Field, NewNameString))
        {
            continue;
        }
		if (Cast<UClass>(Field) && (Cast<UClass>(Field)->IsChildOf<UInterface>() || Cast<UClass>(Field)->HasAnyClassFlags(CLASS_Interface)))
		{
			if (!IsFieldNameUsed(FName(*(TEXT("U") + NewNameString))) && !IsFieldNameUsed(FName(*(TEXT("I") + NewNameString))))
			{
				break;
			}
		}
		else if (!IsFieldNameUsed(FName(*NewNameString)))
		{
			break;
		}
	}

	UniqueFieldNameArray.Add(FRegisterNameField(*NewNameString, Field));

    if (!ConflictEvidence.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("Generated type for %s renamed to %s because of native identifier conflict: %s"),
            *Field->GetPathName(), *NewNameString, *ConflictEvidence);
    }

	return *NewNameString;
}

void UBlueprintNativizationUniqueNameSubsystem::BeginNameLookup()
{
    NativeFieldNames.Reset();
    NativeReflectedFieldNames.Reset();
    SourceNameConflicts.Reset();
    PreviousFieldNames.Reset();
    const FString ModuleName = GetDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName;
    const FString CacheFile = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), ModuleName, TEXT("BlueprintNativizationCache.bpncache"));
    FString CacheText;
    TSharedPtr<FJsonObject> Cache;
    FString CachedModule;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (FFileHelper::LoadFileToString(CacheText, *CacheFile) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CacheText), Cache) && Cache &&
        Cache->TryGetStringField(TEXT("ModuleName"), CachedModule) && CachedModule == ModuleName &&
        Cache->TryGetArrayField(TEXT("items"), Items))
    {
        const FString ModulePath = TEXT("/Script/") + ModuleName + TEXT(".");
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            const TSharedPtr<FJsonObject>* Mapping = nullptr;
            FString OriginalField;
            FString NewFieldPath;
            FString NewFieldName;
            if (Item->TryGetObject(Mapping) && (*Mapping)->TryGetStringField(TEXT("OriginalField"), OriginalField) &&
                (*Mapping)->TryGetStringField(TEXT("NewFieldPath"), NewFieldPath) && NewFieldPath.StartsWith(ModulePath) &&
                (*Mapping)->TryGetStringField(TEXT("NewFieldName"), NewFieldName))
            {
                UField* NativeField = FindObject<UField>(nullptr, *NewFieldPath);
                if (NativeField && UBlueprintNativizationLibrary::IsCppDefinedField(NativeField) && GetDefaultBaseName(NativeField) == NewFieldName)
                {
                    PreviousFieldNames.Add(OriginalField, FName(*NewFieldName));
                }
            }
        }
    }
    TSet<FString> CandidateNames;
    for (TObjectIterator<UField> FieldIterator; FieldIterator; ++FieldIterator)
    {
        UField* Field = *FieldIterator;
        if (UBlueprintNativizationLibrary::IsCppDefinedField(Field) ||
            (!Field->IsA<UClass>() && !Field->IsA<UUserDefinedStruct>() && !Field->IsA<UUserDefinedEnum>()))
        {
            continue;
        }
        FString BaseName = SanitizeIdentifier(GetDefaultBaseName(Field));
        if (BaseName.EndsWith(TEXT("_C")))
        {
            BaseName.LeftChopInline(2);
        }
        const FString QualifiedName = GetModuleQualifiedFieldName(Field, BaseName);
        const UClass* Class = Cast<UClass>(Field);
        if (Class && Class->HasAnyClassFlags(CLASS_Interface))
        {
            CandidateNames.Add(TEXT("U") + BaseName);
            CandidateNames.Add(TEXT("I") + BaseName);
            CandidateNames.Add(TEXT("U") + QualifiedName);
            CandidateNames.Add(TEXT("I") + QualifiedName);
        }
        else
        {
            CandidateNames.Add(BaseName);
            CandidateNames.Add(QualifiedName);
        }
        if (const FName* PreviousName = PreviousFieldNames.Find(UBlueprintNativizationLibrary::GetStableFieldIdentifier(Field)))
        {
            if (Class && Class->HasAnyClassFlags(CLASS_Interface))
            {
                CandidateNames.Add(TEXT("U") + PreviousName->ToString());
                CandidateNames.Add(TEXT("I") + PreviousName->ToString());
            }
            else
            {
                CandidateNames.Add(PreviousName->ToString());
            }
        }
    }
    FString SourceLookupReport;
    if (!FNativeModuleCache::FindNativeTypeNameConflicts(CandidateNames, ModuleName, SourceNameConflicts, SourceLookupReport))
    {
        FNativeApiCatalog::AddError(SourceLookupReport);
    }
    FConfigFile HeaderToolConfiguration;
    HeaderToolConfiguration.Combine(FPaths::Combine(FPaths::EngineDir(), TEXT("Programs/UnrealHeaderTool/Config/DefaultEngine.ini")));
    TArray<FString> ReservedStructNames;
    HeaderToolConfiguration.GetArray(TEXT("UnrealHeaderTool"), TEXT("StructsWithTPrefix"), ReservedStructNames);
    for (const FString& StructName : ReservedStructNames)
    {
        NativeFieldNames.Add(FName(*(TEXT("F") + StructName)));
    }
    for (TObjectIterator<UField> FieldIterator; FieldIterator; ++FieldIterator)
    {
        if (UBlueprintNativizationLibrary::IsCppDefinedField(*FieldIterator))
        {
            if (FieldIterator->GetOutermost()->GetName() == TEXT("/Script/") + ModuleName)
            {
                continue;
            }
            NativeFieldNames.Add(FName(*GetDefaultBaseName(*FieldIterator)));
            if (FieldIterator->IsA<UClass>() || FieldIterator->IsA<UScriptStruct>() || FieldIterator->IsA<UEnum>())
            {
                NativeReflectedFieldNames.Add(FieldIterator->GetFName());
            }

            if (FieldIterator->IsA<UScriptStruct>())
            {
                NativeFieldNames.Add(FName(*(TEXT("F") + FieldIterator->GetName())));
            }
        }
    }
    bNameLookupActive = true;
}

void UBlueprintNativizationUniqueNameSubsystem::EndNameLookup()
{
    bNameLookupActive = false;
    NativeFieldNames.Reset();
    NativeReflectedFieldNames.Reset();
    SourceNameConflicts.Reset();
    PreviousFieldNames.Reset();
}

bool UBlueprintNativizationUniqueNameSubsystem::IsFieldNameUsed(FName Name)
{
    if (!bNameLookupActive)
    {
        return GetAllFieldNames().Contains(Name);
    }
    return NativeFieldNames.Contains(Name) || SourceNameConflicts.Contains(Name.ToString()) || UniqueFieldNameArray.ContainsByPredicate([Name](const FRegisterNameField& RegisteredField)
    {
        return RegisteredField.Name == Name;
    });
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
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
		{
			FFunctionDescriptor NativeMethod;
			if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(Function->GetOwnerClass(), Function->GetName(), NativeMethod))
			{
				Names.AddUnique(FName(*NativeMethod.FunctionName));
			}
		}
	}

    for (UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
    {
        const FRegisterNameFunction* RegisteredFunctions = UniqueFunctionNameArray.FindByKey(CurrentClass);
        if (RegisteredFunctions)
        {
            for (const FNodeNamePair& RegisteredFunction : RegisteredFunctions->NodeNamePairs)
            {
                Names.AddUnique(RegisteredFunction.Name);
            }
        }
    }

    for (UClass* NativeClass = Class; NativeClass; NativeClass = NativeClass->GetSuperClass())
    {
        if (!NativeClass->HasAnyClassFlags(CLASS_Native))
        {
            continue;
        }
        const TSharedPtr<FJsonObject> NativeType = FNativeApiCatalog::FindType(NativeClass);
        if (!NativeType)
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& CallableValue : NativeType->GetArrayField(TEXT("Callables")))
        {
            const TSharedPtr<FJsonObject> Callable = CallableValue->AsObject();
            if (Callable->GetBoolField(TEXT("Virtual")) && Callable->GetBoolField(TEXT("AccessVerified")))
            {
                Names.AddUnique(FName(*Callable->GetStringField(TEXT("Name"))));
            }
        }
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

    for (UClass* Layer = Class; Layer; Layer = Layer->GetSuperClass())
    {
        if (Layer->IsNative())
        {
            if (const TSharedPtr<FJsonObject> Type = FNativeApiCatalog::FindType(Layer))
            {
                for (const TSharedPtr<FJsonValue>& Member : Type->GetArrayField(TEXT("Members")))
                {
                    Names.Add(FName(*Member->AsObject()->GetStringField(TEXT("Name"))));
                }
            }
        }
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

    UFunction* OriginalFunction = Class->FindFunctionByName(
        FName(*UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(EntryNode)));
    UFunction* RootFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(OriginalFunction);
    if (RootFunction && RootFunction->GetOwnerClass()->ClassGeneratedBy != Class->ClassGeneratedBy)
    {
        const FName InheritedName = GetUniqueFunctionName(RootFunction, RootFunction->GetName());
        UniqueFunctionNameArray.FindByKey(Class)->NodeNamePairs.Add({EntryNode, InheritedName});
        return InheritedName;
    }
    TArray<FName> ReservedFunctionNames = GetAllFunctionNames(Class->GetSuperClass());
    for (UClass* Layer = Class.Get(); Layer; Layer = Layer->GetSuperClass())
    {
        for (const FImplementedInterface& ImplementedInterface : Layer->Interfaces)
        {
            for (TFieldIterator<UFunction> InterfaceFunction(ImplementedInterface.Class); InterfaceFunction; ++InterfaceFunction)
            {
                ReservedFunctionNames.AddUnique(GetUniqueFunctionName(*InterfaceFunction, InterfaceFunction->GetName()));
            }
        }
    }
    ExistingFunctionEntry = UniqueFunctionNameArray.FindByKey(Class);
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

		if (!IsFieldNameUsed(FName(*UniqueNameString)) &&
            !ReservedFunctionNames.Contains(FName(*UniqueNameString)))
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
	if (Function && Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
	{
		return *SanitizeIdentifier(Function->GetName());
	}

	UK2Node* EntryNode = Cast<UK2Node>(
		UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(
			UBlueprintNativizationLibrary::GetBlueprintFromFunction(Function),
			Function->GetFName())
	);

    if (!EntryNode)
    {
        if (UFunction* ParentFunction = Function->GetSuperFunction())
        {
            return GetUniqueFunctionName(ParentFunction, PredictName);
        }
        FNativeApiCatalog::AddError(TEXT("Cannot resolve a generated function entry: ") + Function->GetPathName());
        return NAME_None;
    }
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
						if (UFunction* GeneratedFunction = OwnerClass->FindFunctionByName(Function->GetFName()))
                        {
                            Property = UBlueprintNativizationLibrary::FindFunctionProperty(GeneratedFunction, Property->GetFName(), Property->HasAnyPropertyFlags(CPF_Parm));
                        }
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
	
    const UFunction* PropertyFunction = Property->GetOwner<UFunction>();
	if (UBlueprintNativizationLibrary::IsCppDefinedProperty(Property) &&
        (!PropertyFunction || !PropertyFunction->HasAnyFunctionFlags(FUNC_Delegate)))
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
    if (OwnerClass)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
        {
            for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
            {
                if (Property->GetFName() == Timeline->GetDirectionPropertyName())
                {
                    BasePropertyName = Timeline->GetVariableName().ToString() + TEXT("Direction");
                }
                auto UseTrackName = [&](const auto& Tracks)
                {
                    for (const auto& Track : Tracks)
                    {
                        if (Property->GetFName() == Track.GetPropertyName())
                        {
                            BasePropertyName = Timeline->GetVariableName().ToString() + Track.GetTrackName().ToString();
                        }
                    }
                };
                UseTrackName(Timeline->FloatTracks);
                UseTrackName(Timeline->VectorTracks);
                UseTrackName(Timeline->LinearColorTracks);
            }
        }
    }
    if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible) && Property->GetName().EndsWith(TEXT("_RefProperty")))
    {
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            if (UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(OwnerClass->GetDefaultObject()))
            {
                BasePropertyName = Value->GetName();
            }
        }
    }
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

		if (CheckUnquieVarName(UniqueNameString, OwnerClass, ExistingRegister))
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

FName UBlueprintNativizationUniqueNameSubsystem::GetLambdaUniqueVariableName(FString LambdaMainName, UClass* OwningClass, const FString& SourceIdentity)
{
	return GenerateUniqueLambdaVariableName(LambdaMainName, OwningClass, nullptr, nullptr, nullptr, SourceIdentity);
}

FName UBlueprintNativizationUniqueNameSubsystem::GenerateUniqueLambdaVariableName(
	const FString& LambdaMainName,
	UClass* OwningClass,
	UFunction* OwningFunction,
	UK2Node* LambdaNode,
	UK2Node* EntryNode,
	const FString& SourceIdentity
)
{
	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwningClass, OwningFunction, EntryNode, false));

	if (!ExistingRegister)
	{
		FRegisterVariable NewRegister(OwningClass, OwningFunction, EntryNode, false);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister = UniqueVarNamesArray.FindByKey(NewRegister);
	}

	FString RegistrationKey = LambdaMainName;
	if (!SourceIdentity.IsEmpty())
	{
		RegistrationKey += TEXT(":") + SourceIdentity;
	}
	for (const FPropertyNamePair& Pair : ExistingRegister->PropertyNamePairs)
	{
		if (Pair.LambdaNode == LambdaNode && Pair.LambdaMainName == RegistrationKey)
		{
			return Pair.Name;
		}
	}

	int& Counter = ExistingRegister->Counters.FindOrAdd(*LambdaMainName);
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

    FString UniqueNameString;
    bool bNameAvailable = false;
    do
    {
        FString Suffix;
        if (Counter > 0)
        {
            Suffix = FString::FromInt(Counter);
        }
        UniqueNameString = SanitizeIdentifier(LambdaMainName);
        if (Settings->bEnableGenerateSuffix)
        {
            UniqueNameString += TEXT("_GeneratedValue");
        }
        UniqueNameString += Suffix;
        ++Counter;
        bNameAvailable = CheckUnquieVarName(UniqueNameString, OwningClass, ExistingRegister) &&
            !ExistingRegister->PropertyNamePairs.ContainsByPredicate([&](const FPropertyNamePair& Existing)
            {
                return Existing.Name == FName(*UniqueNameString);
            });
    }
    while (!bNameAvailable);

	ExistingRegister->PropertyNamePairs.Add(FPropertyNamePair(nullptr, nullptr, *UniqueNameString, LambdaNode, RegistrationKey));
	return *UniqueNameString;
}


FName UBlueprintNativizationUniqueNameSubsystem::GenerateConstructorLambdaUniqueVariableName(FString LambdaMainName, UClass* Class)
{
	LambdaMainName = SanitizeIdentifier(LambdaMainName);
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


const FPropertyNamePair* UBlueprintNativizationUniqueNameSubsystem::FindPersistentPinVariable(UEdGraphPin* Pin) const
{
    for (const FRegisterVariable& Scope : UniqueVarNamesArray)
    {
        if (!Scope.Function && !Scope.GeneratedFunctionEntryNode)
        {
            if (const FPropertyNamePair* Variable = Scope.PropertyNamePairs.FindByKey(Pin))
            {
                return Variable;
            }
        }
    }
    return nullptr;
}

bool UBlueprintNativizationUniqueNameSubsystem::IsPersistentPinVariable(UEdGraphPin* Pin) const
{
    return FindPersistentPinVariable(Pin) != nullptr;
}

FName UBlueprintNativizationUniqueNameSubsystem::GetUniquePinVariableName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!Pin || !Pin->GetOwningNode() || !Cast<UK2Node>(Pin->GetOwningNode())->GetBlueprint())
	{
		return NAME_None;
	}
	
	UBlueprint* Blueprint = Cast<UK2Node>(Pin->GetOwningNode())->GetBlueprint();
	UClass* OwningClass = Blueprint->GeneratedClass;
    if (const FPropertyNamePair* Persistent = FindPersistentPinVariable(Pin))
    {
        return Persistent->Name;
    }

	FGenerateFunctionStruct GenerateFunctionStruct;
	if (!UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(Cast<UK2Node>(Pin->GetOwningNode()), EntryNodes, GenerateFunctionStruct))
	{
		return NAME_None;
	}
	UK2Node* EntryNode = GenerateFunctionStruct.Node;

	UFunction* OwningFunction = UBlueprintNativizationLibrary::FindFunctionByFunctionName(Blueprint, *UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(EntryNode));

    if (OwningFunction && (Pin->GetOwningNode()->IsA<UK2Node_FunctionEntry>() || Pin->GetOwningNode()->IsA<UK2Node_Event>()))
    {
        if (FProperty* Parameter = OwningFunction->FindPropertyByName(Pin->PinName))
        {
            return *UBlueprintNativizationLibrary::GetUniquePropertyName(Parameter);
        }
    }

    UFunction* RegistrationFunction = OwningFunction;
    UK2Node* RegistrationEntry = EntryNode;
    if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0 && !CastChecked<UK2Node>(Pin->GetOwningNode())->IsNodePure() &&
        !FGenerationGraphContext::IsFunctionGraph(CastChecked<UK2Node>(Pin->GetOwningNode())) &&
        !EntryNodes.ContainsByPredicate([Pin](const FGenerateFunctionStruct& Entry)
        {
            return Entry.Node == Pin->GetOwningNode();
        }) &&
        !Pin->GetOwningNode()->IsA<UK2Node_Event>() && !Pin->GetOwningNode()->IsA<UK2Node_FunctionEntry>() &&
        !UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(Pin, CastChecked<UK2Node>(Pin->GetOwningNode()), EntryNodes))
    {
        RegistrationFunction = nullptr;
        RegistrationEntry = nullptr;
    }
	FRegisterVariable* ExistingRegister = UniqueVarNamesArray.FindByKey(FRegisterVariable(OwningClass, RegistrationFunction, RegistrationEntry, false));

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
		FRegisterVariable NewRegister(OwningClass, RegistrationFunction, RegistrationEntry, false);
		UniqueVarNamesArray.Add(NewRegister);
		ExistingRegister =UniqueVarNamesArray.FindByKey(NewRegister);
	}

	FString BasePinName = Pin->PinName.ToString();
    if (BasePinName.Len() == 1)
    {
        FString ContextName = Pin->GetOwningNode()->GetClass()->GetName().RightChop(7);
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Pin->GetOwningNode()))
        {
            ContextName = Call->FunctionReference.GetMemberName().ToString();
        }
        BasePinName = ContextName + BasePinName;
    }
    if (Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
    {
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Pin->GetOwningNode()))
        {
            BasePinName = Call->FunctionReference.GetMemberName().ToString() + TEXT("Result");
        }
    }
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

bool UBlueprintNativizationUniqueNameSubsystem::CheckUnquieVarName(FString CheckName, UClass* Class, const FRegisterVariable* VariableScope)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
    if (IsFieldNameUsed(FName(*CheckName)))
    {
        return false;
    }
    if (Class)
    {
        if (!VariableScope)
        {
            for (TFieldIterator<UFunction> Function(Class); Function; ++Function)
            {
                for (TFieldIterator<FProperty> Parameter(*Function); Parameter; ++Parameter)
                {
                    if (Parameter->HasAnyPropertyFlags(CPF_Parm) &&
                        (SanitizeIdentifier(Parameter->GetName()) == CheckName ||
                         SanitizeIdentifier(Parameter->GetDisplayNameText().ToString()) == CheckName))
                    {
                        return false;
                    }
                }
            }
        }
        for (const FRegisterVariable& Registered : UniqueVarNamesArray)
        {
            if (VariableScope && (VariableScope->Function || VariableScope->GeneratedFunctionEntryNode) &&
                (Registered.Function || Registered.GeneratedFunctionEntryNode) &&
                !(Registered == *VariableScope))
            {
                continue;
            }
            if (Registered.Class && (Class->IsChildOf(Registered.Class) || Registered.Class->IsChildOf(Class)))
            {
                for (const FPropertyNamePair& Pair : Registered.PropertyNamePairs)
                {
                    if (Pair.Name == FName(*CheckName))
                    {
                        return false;
                    }
                }
            }
        }
    }
    TArray<FName> AllFieldsNames = GetAllFunctionNames(Class);
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


void UBlueprintNativizationUniqueNameSubsystem::AddReferencedObjects(UObject* Object, FReferenceCollector& Collector)
{
    UBlueprintNativizationUniqueNameSubsystem* Subsystem = CastChecked<UBlueprintNativizationUniqueNameSubsystem>(Object);
    for (FRegisterNameField& Registered : Subsystem->UniqueFieldNameArray)
    {
        Collector.AddReferencedObject(Registered.Field, Subsystem);
    }
    for (FRegisterNameFunction& Registered : Subsystem->UniqueFunctionNameArray)
    {
        Collector.AddReferencedObject(Registered.Class, Subsystem);
        for (FNodeNamePair& Function : Registered.NodeNamePairs)
        {
            Collector.AddReferencedObject(Function.Node, Subsystem);
        }
    }
    for (FRegisterVariable& Registered : Subsystem->UniqueVarNamesArray)
    {
        Collector.AddReferencedObject(Registered.Class, Subsystem);
        Collector.AddReferencedObject(Registered.Function, Subsystem);
        Collector.AddReferencedObject(Registered.GeneratedFunctionEntryNode, Subsystem);
        for (FPropertyNamePair& Member : Registered.PropertyNamePairs)
        {
            Collector.AddReferencedObject(Member.LambdaNode, Subsystem);
            Collector.AddReferencedObject(Member.PropertyOwner, Subsystem);
        }
    }
    Super::AddReferencedObjects(Object, Collector);
}
