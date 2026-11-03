/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/GetSubsystemTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/AudioEngineSubsystem.h"
#include "NativeApiCatalog.h"

FGenerateResultStruct UGetSubsystemTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_GetSubsystem* Getter = CastChecked<UK2Node_GetSubsystem>(Node);
    UClass* Class = CastChecked<UClass>(Getter->GetResultPin()->PinType.PinSubCategoryObject.Get());
    FString Method;
    if (Class->IsChildOf(UGameInstanceSubsystem::StaticClass()))
    {
        Method = TEXT("GetGameInstanceSubsystem");
    }
    else if (Class->IsChildOf(UWorldSubsystem::StaticClass()))
    {
        Method = TEXT("GetWorldSubsystem");
    }
    else if (Class->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
    {
        Method = TEXT("GetLocalPlayerSubsystem");
    }
    else if (Class->IsChildOf(UAudioEngineSubsystem::StaticClass()))
    {
        Method = TEXT("GetAudioEngineSubsystem");
    }
    else
    {
        FNativeApiCatalog::AddError(TEXT("Unsupported subsystem class: ") + Class->GetPathName());
        return {};
    }
    FGenerateResultStruct Context(TEXT("this"));
    if (UEdGraphPin* ContextPin = Getter->GetWorldContextPin(); ContextPin &&
        (!ContextPin->LinkedTo.IsEmpty() || ContextPin->DefaultObject))
    {
        Context = Subsystem->GenerateInputParameterCodeForNode(Node, ContextPin, 0, MacroStack);
    }
    FGenerateResultStruct ClassValue(FRetainedBlueprintGeneration::GetClassValue(Class));
    if (UEdGraphPin* ClassPin = Getter->GetClassPin(); ClassPin && !ClassPin->LinkedTo.IsEmpty())
    {
        ClassValue = Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack);
    }
    Context.Preparations.Append(ClassValue.Preparations);
    Context.Code = TEXT("Cast<") + FRetainedBlueprintGeneration::GetClassName(Class) +
        TEXT(">(USubsystemBlueprintLibrary::") + Method + TEXT("(") + Context.Code + TEXT(", ") + ClassValue.Code + TEXT("))");
    return Context;
}

TSet<FString> UGetSubsystemTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Subsystems/SubsystemBlueprintLibrary.h\""));
    return Includes;
}
