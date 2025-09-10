/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Composite.h"

#include "BlueprintNativizationData.generated.h"

/* 
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find node when you search for it using Blueprint drop-down menu. 
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop-down menu.
*
*	For more info on custom blueprint nodes visit documentation:
*	https://wiki.unrealengine.com/Custom_Blueprint_Node_Creation
*/

template<typename KeyType, typename ValueType>
TMap<ValueType, KeyType> ReverseMap(const TMap<KeyType, ValueType>& OriginalMap)
{
    TMap<ValueType, KeyType> ReversedMap;
    for (const TPair<KeyType, ValueType>& Pair : OriginalMap)
    {
        ReversedMap.Add(Pair.Value, Pair.Key);
    }
    return ReversedMap;
};


USTRUCT(BlueprintType)
struct FNativizationCode
{
    GENERATED_BODY()
public:
    UObject* Object;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationCode")
    FString ObjectName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationCode")
    FString OutputHeaderString;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationCode")
    FString OutputCppString;
};


USTRUCT(BlueprintType)
struct FNativizationModuleResult
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationModuleResult")
    bool Sucsess = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationModuleResult")
    TArray<FNativizationCode> NativizationCodes;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationModuleResult")
    FNativizationCode ModuleCode;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "NativizationModuleResult")
    FString ModuleBuildCsCode;
};

UENUM()
enum class EConversionFunctionFlag : uint8
{
    ConvertToCPP UMETA(DisplayName = "ConvertToCPP"),
    ConvertAsNativeFunction UMETA(DisplayName = "ConvertAsNativeFunction"),
    Ignore UMETA(DisplayName = "Ignore")
};

UENUM()
enum class EConversionPropertyFlag : uint8
{
    ConvertToCPP UMETA(DisplayName = "ConvertToCPP"),
    Ignore UMETA(DisplayName = "Ignore")
};

USTRUCT(BlueprintType)
struct FBlueprintProxy
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, Category = "Blueprints")
    TArray<UBlueprint*> Blueprints;
};



USTRUCT(BlueprintType)
struct FGenerateFunctionStruct
{
    GENERATED_BODY()

public:
    FGenerateFunctionStruct(FName NewName, UK2Node* NewNode)
        : Name(NewName), Node(NewNode)
    {
    }

    FGenerateFunctionStruct() = default;

    UPROPERTY()
    FName Name;

    UPROPERTY()
    FString ReturnType;

    UPROPERTY()
    UK2Node* Node;

    UPROPERTY()
    UFunction* OriginalUFunction = nullptr;

    FName ParentFunctionName;

    UPROPERTY()
    FString FlagsString;

    UPROPERTY()
    bool IsTransientGenerateFunction = false;

    UPROPERTY()
    TArray<FString> CustomParametersString;

    FORCEINLINE bool operator==(const FGenerateFunctionStruct& Other) const
    {
        return Name == Other.Name && Node == Other.Node;
    }
};

template<typename KeyType, typename ValueType>
class TBiMap
{
public:
    TMap<KeyType, ValueType> ForwardMap;
    TMap<ValueType, KeyType> ReverseMap;

    void Add(const KeyType& Key, const ValueType& Value)
    {
        ForwardMap.Add(Key, Value);
        ReverseMap.Add(Value, Key); // ѕотер€ данных возможна, если Value не уникален
    }

    void RemoveByKey(const KeyType& Key)
    {
        if (const ValueType* FoundValue = ForwardMap.Find(Key))
        {
            ReverseMap.Remove(*FoundValue);
            ForwardMap.Remove(Key);
        }
    }

    void RemoveByValue(const ValueType& Value)
    {
        if (const KeyType* FoundKey = ReverseMap.Find(Value))
        {
            ForwardMap.Remove(*FoundKey);
            ReverseMap.Remove(Value);
        }
    }

    const ValueType* FindByKey(const KeyType& Key) const
    {
        return ForwardMap.Find(Key);
    }

    const KeyType* FindByValue(const ValueType& Value) const
    {
        return ReverseMap.Find(Value);
    }
};

USTRUCT()
struct FGenerateResultStruct
{

    GENERATED_BODY()

public:
    UPROPERTY()
    FString Code;

    UPROPERTY()
    TSet<FString> Preparations;

    FGenerateResultStruct() = default;
    FGenerateResultStruct(FString NewCode, TSet<FString> NewPreparations)
        : Code(NewCode), Preparations(NewPreparations)
    {
    }
    FGenerateResultStruct(FString NewCode)
        : Code(NewCode)
    {
    }
};


UCLASS()
class UBlueprintNativizationDataLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static  bool FindGenerateFunctionStructByName(
        UPARAM(ref) const TArray<FGenerateFunctionStruct>& GenerateFunctionStructs,
        FName Name, FGenerateFunctionStruct& OutGenerateFunctionStruct
    );

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static bool FindGenerateFunctionStructByNode(
        UPARAM(ref) const TArray<FGenerateFunctionStruct>& GenerateFunctionStructs,
        UK2Node* Node, FGenerateFunctionStruct& OutGenerateFunctionStruct
    );

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static FString FormatNamespaceCodeStyle(FString& Input);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static FString CutBeforeAndIncluding(const FString& Source, const FString& ToFind);


};

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPinExcludeFilter : uint8
{
    None = 0 UMETA(DisplayName = "None"),
    ExecPin = 1 << 0 UMETA(DisplayName = "Exec Pin"),
    DelegatePin = 1 << 1 UMETA(DisplayName = "Delegate Pin"),
    Self = 1 << 1 UMETA(DisplayName = "Self Pin"),
};

ENUM_CLASS_FLAGS(EPinExcludeFilter);

UENUM()
enum class EPinOutputOrInputFilter: uint8
{
    All,
    Input,
    Ouput
};

UENUM()
enum class EPinIncludeOnlyFilter : uint8
{
    None,
    ExecPin,
    DelegatePin,
    OutRefOnlyPin
};

struct FVisitedNodeStack
{
    UK2Node* Node;
    FString EntryPinName;

    FVisitedNodeStack(UK2Node* NewK2Node, FString NewEntryPinName)
    {
        Node = NewK2Node;
        EntryPinName = NewEntryPinName;
    }

    bool operator==(const FVisitedNodeStack& Other) const
    {
        return Node == Other.Node && EntryPinName == Other.EntryPinName;
    }
};