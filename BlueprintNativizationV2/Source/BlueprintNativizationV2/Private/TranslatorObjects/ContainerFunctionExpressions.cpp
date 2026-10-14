#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/BlueprintMapLibrary.h"
#include "Kismet/BlueprintSetLibrary.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Kismet/BlueprintInstancedStructLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "NativeFunctionExpressions.h"

static const FString* FindContainerFunctionTranslation(UFunction* Function)
{
    UClass* Owner = Function->GetOwnerClass();
    if (Owner == UKismetArrayLibrary::StaticClass() || Owner == UBlueprintMapLibrary::StaticClass() || Owner == UBlueprintSetLibrary::StaticClass())
    {
        static const FString Equality = TEXT("auto AreEqual = [](const auto& FirstValue, const auto& SecondValue)\n{\nusing ElementType = typename TDecay<decltype(FirstValue)>::Type;\nif constexpr (TModels_V<CStaticStructProvider, ElementType>)\n{\nreturn ElementType::StaticStruct()->CompareScriptStruct(&FirstValue, &SecondValue, 0);\n}\nelse\n{\nreturn FirstValue == SecondValue;\n}\n};\n");
        static const TMap<FName, FString> Expressions = {
            { TEXT("Array_Random"), TEXT("[](const auto& Values, auto& Item, int32& Index)\n{\nIndex = INDEX_NONE;\nItem = {};\nif (!Values.IsEmpty())\n{\nIndex = FMath::RandRange(0, Values.Num() - 1);\nItem = Values[Index];\n}\n}") },
            { TEXT("Array_RandomFromStream"), TEXT("[](const auto& Values, FRandomStream& Stream, auto& Item, int32& Index)\n{\nIndex = INDEX_NONE;\nItem = {};\nif (!Values.IsEmpty())\n{\nIndex = Stream.RandRange(0, Values.Num() - 1);\nItem = Values[Index];\n}\n}") },
            { TEXT("Array_Shuffle"), TEXT("[](auto&& Values)\n{\nfor (int32 Index = 0; Index < Values.Num(); ++Index)\n{\nValues.Swap(Index, FMath::RandRange(Index, Values.Num() - 1));\n}\n}") },
            { TEXT("Array_ShuffleFromStream"), TEXT("[](auto&& Values, FRandomStream& Stream)\n{\nfor (int32 Index = 0; Index < Values.Num(); ++Index)\n{\nValues.Swap(Index, Stream.RandRange(Index, Values.Num() - 1));\n}\n}") },
            { TEXT("Array_Length"), TEXT("[](const auto& Values)\n{\nreturn Values.Num();\n}") },
            { TEXT("Array_IsEmpty"), TEXT("[](const auto& Values)\n{\nreturn Values.IsEmpty();\n}") },
            { TEXT("Array_IsNotEmpty"), TEXT("[](const auto& Values)\n{\nreturn !Values.IsEmpty();\n}") },
            { TEXT("Array_LastIndex"), TEXT("[](const auto& Values)\n{\nreturn Values.Num() - 1;\n}") },
            { TEXT("Array_IsValidIndex"), TEXT("[](const auto& Values, int32 Index)\n{\nreturn Values.IsValidIndex(Index);\n}") },
            { TEXT("Array_Contains"), TEXT("[](const auto& Values, const auto& Item)\n{\n") + Equality + TEXT("return Values.ContainsByPredicate([&](const auto& Value)\n{\nreturn AreEqual(Value, Item);\n});\n}") },
            { TEXT("Array_Find"), TEXT("[](const auto& Values, const auto& Item)\n{\n") + Equality + TEXT("return Values.IndexOfByPredicate([&](const auto& Value)\n{\nreturn AreEqual(Value, Item);\n});\n}") },
            { TEXT("Array_Identical"), TEXT("[](const auto& First, const auto& Second)\n{\n") + Equality + TEXT("if (First.Num() != Second.Num())\n{\nreturn false;\n}\nfor (int32 ElementIndex = 0; ElementIndex < First.Num(); ++ElementIndex)\n{\nif (!AreEqual(First[ElementIndex], Second[ElementIndex]))\n{\nreturn false;\n}\n}\nreturn true;\n}") },
            { TEXT("Array_Add"), TEXT("[](auto&& Values, const auto& Item)\n{\nreturn Values.Add(Item);\n}") },
            { TEXT("Array_AddUnique"), TEXT("[](auto&& Values, const auto& Item) -> int32\n{\n") + Equality + TEXT("if (Values.ContainsByPredicate([&](const auto& Value)\n{\nreturn AreEqual(Value, Item);\n}))\n{\nreturn INDEX_NONE;\n}\nreturn Values.Add(Item);\n}") },
            { TEXT("Array_Append"), TEXT("[](auto&& Values, const auto& Additional)\n{\nif (static_cast<const void*>(&Values) == static_cast<const void*>(&Additional))\n{\nauto AdditionalValues = Additional;\nValues.Append(AdditionalValues);\n}\nelse\n{\nValues.Append(Additional);\n}\n}") },
            { TEXT("Array_Clear"), TEXT("[](auto&& Values)\n{\nValues.Reset();\n}") },
            { TEXT("Array_RemoveItem"), TEXT("[](auto&& Values, const auto& Item)\n{\n") + Equality + TEXT("return Values.RemoveAll([&](const auto& Value)\n{\nreturn AreEqual(Value, Item);\n}) > 0;\n}") },
            { TEXT("Array_Remove"), TEXT("[](auto&& Values, int32 Index)\n{\nif (Values.IsValidIndex(Index))\n{\nValues.RemoveAt(Index);\n}\n}") },
            { TEXT("Array_Insert"), TEXT("[](auto&& Values, const auto& Item, int32 Index)\n{\nif (Index >= 0 && Index <= Values.Num())\n{\nValues.Insert(Item, Index);\n}\n}") },
            { TEXT("Array_Resize"), TEXT("[](auto&& Values, int32 Size)\n{\nif (Size >= 0)\n{\nValues.SetNum(Size);\n}\n}") },
            { TEXT("Array_Get"), TEXT("[](const auto& Values, int32 Index, auto& Item)\n{\nItem = {};\nif (Values.IsValidIndex(Index))\n{\nItem = Values[Index];\n}\n}") },
            { TEXT("Array_Set"), TEXT("[](auto&& Values, int32 Index, const auto& Item, bool bSizeToFit)\n{\nif (Index >= 0 && bSizeToFit && Index >= Values.Num())\n{\nValues.SetNum(Index + 1);\n}\nif (Values.IsValidIndex(Index))\n{\nValues[Index] = Item;\n}\n}") },
            { TEXT("Array_Swap"), TEXT("[](auto&& Values, int32 FirstIndex, int32 SecondIndex)\n{\nif (Values.IsValidIndex(FirstIndex) && Values.IsValidIndex(SecondIndex))\n{\nValues.Swap(FirstIndex, SecondIndex);\n}\n}") },
            { TEXT("Array_Reverse"), TEXT("[](auto&& Values)\n{\nfor (int32 Index = 0; Index < Values.Num() / 2; ++Index)\n{\nValues.Swap(Index, Values.Num() - Index - 1);\n}\n}") },
            { TEXT("Map_GetLastIndex"), TEXT("[](const auto& Values)\n{\nreturn Values.Num() - 1;\n}") },
            { TEXT("Map_GetKeyValueByIndex"), TEXT("[](const auto& Values, int32 Index, auto& Key, auto& Value)\n{\nint32 EntryIndex = 0;\nfor (const auto& Entry : Values)\n{\nif (EntryIndex == Index)\n{\nKey = Entry.Key;\nValue = Entry.Value;\nbreak;\n}\n++EntryIndex;\n}\n}") },
            { TEXT("Set_GetLastIndex"), TEXT("[](const auto& Values)\n{\nreturn Values.Num() - 1;\n}") },
            { TEXT("Set_GetItemByIndex"), TEXT("[](const auto& Values, int32 Index, auto& Item)\n{\nint32 EntryIndex = 0;\nfor (const auto& Value : Values)\n{\nif (EntryIndex == Index)\n{\nItem = Value;\nbreak;\n}\n++EntryIndex;\n}\n}") },
            { TEXT("Map_Length"), TEXT("[](const auto& Values)\n{\nreturn Values.Num();\n}") },
            { TEXT("Map_IsEmpty"), TEXT("[](const auto& Values)\n{\nreturn Values.IsEmpty();\n}") },
            { TEXT("Map_IsNotEmpty"), TEXT("[](const auto& Values)\n{\nreturn !Values.IsEmpty();\n}") },
            { TEXT("Map_Contains"), TEXT("[](const auto& Values, const auto& Key)\n{\nreturn Values.Contains(Key);\n}") },
            { TEXT("Map_Find"), TEXT("[](const auto& Values, const auto& Key, auto& Value)\n{\nif (const auto* FoundValue = Values.Find(Key))\n{\nValue = *FoundValue;\nreturn true;\n}\nValue = {};\nreturn false;\n}") },
            { TEXT("Map_Add"), TEXT("[](auto&& Values, const auto& Key, const auto& Value)\n{\nValues.Add(Key, Value);\n}") },
            { TEXT("Map_Remove"), TEXT("[](auto&& Values, const auto& Key)\n{\nreturn Values.Remove(Key) > 0;\n}") },
            { TEXT("Map_Clear"), TEXT("[](auto&& Values)\n{\nValues.Reset();\n}") },
            { TEXT("Map_Keys"), TEXT("[](const auto& Values, auto& Keys)\n{\nValues.GetKeys(Keys);\n}") },
            { TEXT("Map_Values"), TEXT("[](const auto& Values, auto& Result)\n{\nValues.GenerateValueArray(Result);\n}") },
            { TEXT("Set_Length"), TEXT("[](const auto& Values)\n{\nreturn Values.Num();\n}") },
            { TEXT("Set_IsEmpty"), TEXT("[](const auto& Values)\n{\nreturn Values.IsEmpty();\n}") },
            { TEXT("Set_IsNotEmpty"), TEXT("[](const auto& Values)\n{\nreturn !Values.IsEmpty();\n}") },
            { TEXT("Set_Contains"), TEXT("[](const auto& Values, const auto& Item)\n{\nreturn Values.Contains(Item);\n}") },
            { TEXT("Set_Add"), TEXT("[](auto&& Values, const auto& Item)\n{\nValues.Add(Item);\n}") },
            { TEXT("Set_Remove"), TEXT("[](auto&& Values, const auto& Item)\n{\nreturn Values.Remove(Item) > 0;\n}") },
            { TEXT("Set_Clear"), TEXT("[](auto&& Values)\n{\nValues.Reset();\n}") },
            { TEXT("Set_AddItems"), TEXT("[](auto&& Values, const auto& Items)\n{\nfor (const auto& Item : Items)\n{\nValues.Add(Item);\n}\n}") },
            { TEXT("Set_RemoveItems"), TEXT("[](auto&& Values, const auto& Items)\n{\nfor (const auto& Item : Items)\n{\nValues.Remove(Item);\n}\n}") },
            { TEXT("Set_ToArray"), TEXT("[](const auto& Values, auto& Result)\n{\nResult = Values.Array();\n}") },
            { TEXT("Set_Intersection"), TEXT("[](const auto& First, const auto& Second, auto& Result)\n{\nResult = First.Intersect(Second);\n}") },
            { TEXT("Set_Union"), TEXT("[](const auto& First, const auto& Second, auto& Result)\n{\nResult = First.Union(Second);\n}") },
            { TEXT("Set_Difference"), TEXT("[](const auto& First, const auto& Second, auto& Result)\n{\nResult = First.Difference(Second);\n}") },
        };
        return Expressions.Find(Function->GetFName());
    }
    return nullptr;
}

bool UCallFunctionTranslatorObject::HasContainerFunctionTranslation(UFunction* Function)
{
    return Function->HasAnyFunctionFlags(FUNC_Static) && FindContainerFunctionTranslation(Function) != nullptr;
}

FString UCallFunctionTranslatorObject::GenerateStaticFunctionCall(UFunction* Function, const FString& Arguments, const TArray<FString>* ArgumentExpressions, UK2Node_CallFunction* Call)
{
    const FString BehaviorTreeCall = FNativeFunctionExpressions::BehaviorTree(Function, Arguments, Call);
    if (!BehaviorTreeCall.IsEmpty())
    {
        return BehaviorTreeCall;
    }
    if (ArgumentExpressions)
    {
        TArray<FString> ArgumentTypes;
        if (Call)
        {
            for (TFieldIterator<FProperty> Parameter(Function); Parameter; ++Parameter)
            {
                if (!Parameter->HasAnyPropertyFlags(CPF_Parm) || Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    continue;
                }
                FString ArgumentType;
                UEdGraphPin* Input = Call->FindPin(Parameter->GetFName());
                if (Input && Input->LinkedTo.Num() == 1)
                {
                    ArgumentType = UBlueprintNativizationLibrary::GetPinType(Input->LinkedTo[0]->PinType, false);
                }
                ArgumentTypes.Add(ArgumentType);
            }
        }
        const FString Expression = FNativeFunctionExpressions::Arithmetic(Function, *ArgumentExpressions, ArgumentTypes);
        if (!Expression.IsEmpty())
        {
            return Expression;
        }
    }
    UClass* Owner = Function->GetOwnerClass();
    if (Owner == UKismetArrayLibrary::StaticClass() || Owner == UBlueprintMapLibrary::StaticClass() || Owner == UBlueprintSetLibrary::StaticClass())
    {
        const FString FunctionName = Function->GetName();
        if (FunctionName.EndsWith(TEXT("_Length")))
        {
            return TEXT("(") + Arguments + TEXT(").Num()");
        }
        if (FunctionName.EndsWith(TEXT("_IsEmpty")))
        {
            return TEXT("(") + Arguments + TEXT(").IsEmpty()");
        }
        if (FunctionName.EndsWith(TEXT("_IsNotEmpty")))
        {
            return TEXT("!(") + Arguments + TEXT(").IsEmpty()");
        }
        if (FunctionName.EndsWith(TEXT("_LastIndex")) || FunctionName.EndsWith(TEXT("_GetLastIndex")))
        {
            return TEXT("((") + Arguments + TEXT(").Num() - 1)");
        }
        if (FunctionName.EndsWith(TEXT("_Clear")))
        {
            return TEXT("(") + Arguments + TEXT(").Reset()");
        }
    }
    if (const FString* Expression = FindContainerFunctionTranslation(Function))
    {
        FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/Class.h\""));
        return TEXT("(") + *Expression + TEXT(")(") + Arguments + TEXT(")");
    }
    if (Owner == UBlueprintInstancedStructLibrary::StaticClass())
    {
        if (Function->GetFName() == TEXT("MakeInstancedStruct"))
        {
            return TEXT("([](const auto& Value)\n{\nusing ValueType = typename TDecay<decltype(Value)>::Type;\nFInstancedStruct Result;\nResult.InitializeAs(TBaseStructure<ValueType>::Get(), reinterpret_cast<const uint8*>(&Value));\nreturn Result;\n})(") + Arguments + TEXT(")");
        }
        if (Function->GetFName() == TEXT("SetInstancedStructValue"))
        {
            return TEXT("([](FInstancedStruct& Instance, const auto& Value)\n{\nusing ValueType = typename TDecay<decltype(Value)>::Type;\nInstance.InitializeAs(TBaseStructure<ValueType>::Get(), reinterpret_cast<const uint8*>(&Value));\n})(") + Arguments + TEXT(")");
        }
        if (Function->GetFName() == TEXT("GetInstancedStructValue"))
        {
            return TEXT("([](EStructUtilsResult& Result, const FInstancedStruct& Instance, auto& Value)\n{\nusing ValueType = typename TDecay<decltype(Value)>::Type;\nUScriptStruct* ValueStructure = TBaseStructure<ValueType>::Get();\nResult = EStructUtilsResult::NotValid;\nif (Instance.IsValid() && Instance.GetScriptStruct()->IsChildOf(ValueStructure))\n{\nValueStructure->CopyScriptStruct(&Value, Instance.GetMemory());\nResult = EStructUtilsResult::Valid;\n}\n})(") + Arguments + TEXT(")");
        }
    }
    FString PublicHeader;
    if (Function->IsNative() &&
        (!GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(Owner, PublicHeader) ||
            !FNativeApiCatalog::CanCallFunctionDirectly(Function)))
    {
        FString Expression;
        TSet<FString> Includes;
        TSet<FString> Modules;
        if (FNativeApiCatalog::FindPublicForwarding(Function, Expression, Includes, Modules))
        {
            for (const FString& Include : Includes)
            {
                FNativeGenerationContext::AddImplementationInclude(FString::Format(TEXT("#include \"{0}\""), {Include}));
            }
            FStructTranslationResult Requirements;
            Requirements.ModuleDependencies = Modules.Array();
            FNativeGenerationContext::AddRequirements(Requirements);
            return TEXT("(") + Expression + TEXT(")(") + Arguments + TEXT(")");
        }
    }
    if (Owner == UDataTableFunctionLibrary::StaticClass() && Function->GetFName() == TEXT("GetDataTableRowFromName"))
    {
        return TEXT("([](UDataTable* Table, FName RowName, auto& Row)\n{\nusing RowType = typename TDecay<decltype(Row)>::Type;\nif (Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(RowType::StaticStruct()))\n{\nreturn UDataTableFunctionLibrary::Generic_GetDataTableRowFromName(Table, RowName, &Row);\n}\nreturn false;\n})(") + Arguments + TEXT(")");
    }
    if (Owner->GetName() == TEXT("GameplayStatics") && Function->GetFName() == TEXT("GetAllActorsOfClass"))
    {
        return TEXT("[](const UObject* Context, TSubclassOf<AActor> ActorClass, auto& Result)\n{\nTArray<AActor*> FoundActors;\nUGameplayStatics::GetAllActorsOfClass(Context, ActorClass, FoundActors);\nResult.Reset();\nusing ElementType = typename TDecay<decltype(Result)>::Type::ElementType;\nusing ActorType = typename TRemovePointer<ElementType>::Type;\nfor (AActor* Actor : FoundActors)\n{\nif (ActorType* TypedActor = Cast<ActorType>(Actor))\n{\nResult.Add(TypedActor);\n}\n}\n}( ") + Arguments + TEXT(")");
    }
    return UBlueprintNativizationLibrary::GetUniqueFieldName(Owner) + TEXT("::") +
        UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT("")) + TEXT("(") + Arguments + TEXT(")");
}
