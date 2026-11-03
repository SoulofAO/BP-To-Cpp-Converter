#pragma once
#include "BlueprintNativizationData.h"
class UNativizationV2Subsystem;

class FCreationNodeTranslation
{
public:
    static FString Input(UK2Node* Node, UEdGraphPin* Pin, const TArray<UK2Node*>& MacroStack,
        UNativizationV2Subsystem* Subsystem, const FString& Stage, FString& Content, const FString& Default = TEXT("nullptr"));
    static FString Result(UK2Node* Node, UEdGraphPin* Pin, UNativizationV2Subsystem* Subsystem, FString& Content);
    static FString AssignExposedProperties(UK2Node* Node, UClass* Class, const FString& Target,
        const TArray<UK2Node*>& MacroStack, UNativizationV2Subsystem* Subsystem);
};
