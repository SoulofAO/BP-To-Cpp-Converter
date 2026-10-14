/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationSettings.h"
#include "TranslatorObjects/NativeValueStructTranslator.h"
#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "TranslatorObjects/BranchTranslatorObject.h"
#include "TranslatorObjects/ClassDefaultsTranslatorObject.h"
#include "TranslatorObjects/InputEventTranslatorObject.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "TranslatorObjects/Latens/DelayNextTickTranslatorObject.h"
#include "TranslatorObjects/Latens/RetriggerableDelayTranslatorObject.h"
#include "TranslatorObjects/Latens/AsyncLoadAssetTranslatorObject.h"
#include "TranslatorObjects/Latens/AIMoveToTranslatorObject.h"
#include "TranslatorObjects/Cycles/ForEachLoopTranslatorObject.h"
#include "TranslatorObjects/Cycles/ForLoopTranslatorObject.h"
#include "TranslatorObjects/MoveComponentToTranslatorObject.h"
#include "TranslatorObjects/SequenceTranslatorObject.h"
#include "TranslatorObjects/MultiGateTranslatorObject.h"
#include "TranslatorObjects/DoOnceMultiInputTranslatorObject.h"
#include "TranslatorObjects/TimelineTranslatorObject.h"
#include "TranslatorObjects/VariableSetTranslatorObject.h"
#include "TranslatorObjects/Cycles/WhileLoopTranslatorObject.h"
#include "TranslatorObjects/VariableGetTranslatorObject.h"
#include "TranslatorObjects/TemporaryVariableTranslatorObject.h"
#include "TranslatorObjects/MakeArrayTranslatorObject.h"
#include "TranslatorObjects/PromotabaleOperatorObject.h"
#include "TranslatorObjects/SelfTranslatorObject.h"
#include "TranslatorObjects/LiteralTranslatorObject.h"
#include "TranslatorObjects/Delegates/CallDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/AddDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/RemoveDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/RemoveAllDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/CreateDelegateTranslatorObject.h"
#include "TranslatorObjects/DynamicCastTranslatorObject.h"

#include "TranslatorObjects/MakeStructTranslatorObject.h"
#include "TranslatorObjects/BreakStructTranslatorObject.h"
#include "TranslatorObjects/MakeSetTranslatorObject.h"
#include "TranslatorObjects/MakeMapTranslatorObject.h"
#include "TranslatorObjects/SetMemberStructTranslatorObject.h"
#include "TranslatorObjects/SelectTranslatorObject.h"
#include "TranslatorObjects/SwitchTranslatorObject.h"
#include "TranslatorObjects/Array/GetArrayItemTranslatorObject.h"

#include "TranslatorObjects/CastByteToEnumTranslatorObject.h"
#include "TranslatorObjects/EnumNameAsStringTranslatorObject.h"

#include "TranslatorObjects/CreateTranslators/AddComponentTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/ConstructObjectTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/CreateWidgetTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/SpawnActorTranslatorObject.h"

#include "TranslatorObjects/GetSubsystemTranslatorObject.h"
#include "TranslatorObjects/EnhancedInputTranslatorObject.h"
#include "TranslatorObjects/ComponentEventTranslatorObject.h"
#include "TranslatorObjects/BinaryOperatorTranslatorObject.h"


#include "TranslatorObjects/ReturnTranslatorObject.h"

#include "Blueprint/UserWidget.h"
#include "ControlRig.h"

#include "BlueprintNativizationLibrary.h"

UBlueprintNativizationV2EditorSettings::UBlueprintNativizationV2EditorSettings()
{
    StructTranslators.Add(UNativeValueStructTranslator::StaticClass());
    StructTranslators.Add(UCollisionStructTranslator::StaticClass());
    StructTranslators.Add(UGameplayTagQueryStructTranslator::StaticClass());
    StructTranslators.Add(USlateValueStructTranslator::StaticClass());
    StructTranslators.Add(UInstancedValueStructTranslator::StaticClass());

	TranslatorBPToCPPObjects =
	{
		// Highest Priority
		UBranchTranslatorObject::StaticClass(),
        UClassDefaultsTranslatorObject::StaticClass(),
        UInputEventTranslatorObject::StaticClass(),
		UDelayTranslatorObject::StaticClass(),
		UDelayNextTickTranslatorObject::StaticClass(),
		UAIMoveToTranslatorObject::StaticClass(),
		URetriggarableDelayTranslatorObject::StaticClass(),
		UForEachLoopTranslatorObject::StaticClass(),
		UForLoopTranslatorObject::StaticClass(),
		UMoveComponentToTranslatorObject::StaticClass(),
		UMultiGateTranslatorObject::StaticClass(),
		UDoOnceMultiInputTranslatorObject::StaticClass(),
		USequenceTranslatorObject::StaticClass(),
		UTimelineTranslatorObject::StaticClass(),
		UVariableSetTranslatorObject::StaticClass(),
		UWhileTranslatorObject::StaticClass(),
		UVariableGetTranslatorObject::StaticClass(),
		UTemporaryVariableTranslatorObject::StaticClass(),
		UMakeArrayTranslatorObject::StaticClass(),
		UPromotabaleOperatorTranslatorObject::StaticClass(),
		USelfTranslatorObject::StaticClass(),
        ULiteralTranslatorObject::StaticClass(),
		UAddDelegateTranslatorObject::StaticClass(),
		UCallDelegateTranslatorObject::StaticClass(),
		URemoveDelegateTranslatorObject::StaticClass(),
		URemoveAllDelegateTranslatorObject::StaticClass(),
		UCreateDelegateTranslatorObject::StaticClass(),
		UDynamicCastTranslatorObject::StaticClass(),

		USetMemberStructTranslatorObject::StaticClass(),
		UGetSubsystemTranslatorObject::StaticClass(),
		UMakeStructTranslatorObject::StaticClass(),
		UBreakStructTranslatorObject::StaticClass(),
		UMakeSetTranslatorObject::StaticClass(),
		UMakeMapTranslatorObject::StaticClass(),
		UGetArrayItemTranslatorObject::StaticClass(),
		UEnhancedInputTranslatorObject::StaticClass(),
		UComponentEventTranslatorObject::StaticClass(),
		UBinaryOperatorTranslatorObject::StaticClass(),
		UAsyncLoadAssetTranslatorObject::StaticClass(),

		UCastByteToEnumTranslatorObject::StaticClass(),
		UEnumNameAsStringTranslatorObject::StaticClass(),

		USelectTranslatorObject::StaticClass(),
		USwitchTranslatorObject::StaticClass(),

		UAddComponentTranslatorObject::StaticClass(),
		UCreateWidgetTranslatorObject::StaticClass(),
		USpawnActorTranslatorObject::StaticClass(),
		UCallParentTranslatorObject::StaticClass(),


		// Lowers Priority
		UConstructObjectTranslatorObject::StaticClass(),
		UCallFunctionTranslatorObject::StaticClass(),
		UReturnTranslatorObject::StaticClass(),

	};

	GlobalVariableNames =
	{
		"GEngine",
		"GWorld",
		"GEditor",
		"GIsEditor",
		"GIsServer",
		"GIsClient",
		"GIsRunning",
		"GFrameCounter",
		"GDeltaTime",
		"GLog",
		"GWarn",
		"GConfig",
		"GEngineLoop",
		"GUObjectArray",
	};

	SetupActionObject = TSoftClassPtr<USetupActionObject>(
		FSoftClassPath(TEXT("/BlueprintNativizationV2/BP_MainSetupActionObject.BP_MainSetupActionObject_C"))
	);



	ColorEditableTextGroups =
	{
		FColorEditableKeywordGroup
		{
			FLinearColor(0.0f, 0.5f, 1.0f),  // ������ ����� ��� �������� ����
			{
				TEXT("if"),
				TEXT("else"),
				TEXT("while"),
				TEXT("for"),
				TEXT("return"),
				TEXT("switch"),
				TEXT("break"),
				TEXT("continue"),
				TEXT("class"),
				TEXT("public"),
				TEXT("static"),
				TEXT("private"),
				TEXT("protected"),
				TEXT("this"),
				TEXT("super"),
				TEXT("function"),
				TEXT("new"),
				TEXT("const"),
				TEXT("true"),
				TEXT("false"),
				TEXT("null"),
				TEXT("NULL"), 
				TEXT("virtual"),
				TEXT("override")
			}
		},
		FColorEditableKeywordGroup
		{
			FLinearColor(0.0f, 0.8f, 0.0f),  // ������ ����� ��� ����������� �����
			{
				TEXT("int"),
				TEXT("float"),
				TEXT("void"),
				TEXT("bool"),
				TEXT("char"),
				TEXT("unsigned"),
				TEXT("long"),
				TEXT("short"),
				TEXT("double"),
				TEXT("FString"),
				TEXT("TArray"),
				TEXT("TMap"),
				TEXT("TSet"),
				TEXT("UObject"),
				TEXT("AActor"),
				TEXT("UClass"),
				TEXT("enum"),
				TEXT("struct"),
			}
		},
		FColorEditableKeywordGroup
		{
			FLinearColor(1.0f, 0.0f, 1.0f),
			{
				TEXT("UPROPERTY"),
				TEXT("UFUNCTION"),
				TEXT("UCLASS"),
				TEXT("GENERATED_BODY()"),
				TEXT("USTRUCT"),
				TEXT("UENUM"),
				TEXT("UINTERFACE"),
				TEXT("UDELEGATE"),
				TEXT("UPARAM")
			}
		}
	};

	ColorEditableTextOperators  =
	{
		TEXT("/*"),
		TEXT("*/"),
		TEXT("//"),
		TEXT("\""),
		TEXT("\'"),
		TEXT("::"),
		TEXT(":"),
		TEXT("+="),
		TEXT("++"),
		TEXT("+"),
		TEXT("--"),
		TEXT("-="),
		TEXT("-"),
		TEXT("("),
		TEXT(")"),
		TEXT("["),
		TEXT("]"),
		TEXT("."),
		TEXT("->"),
		TEXT("!="),
		TEXT("!"),
		TEXT("&="),
		TEXT("~"),
		TEXT("&"),
		TEXT("*="),
		TEXT("*"),
		TEXT("->"),
		TEXT("/="),
		TEXT("/"),
		TEXT("%="),
		TEXT("%"),
		TEXT("<<="),
		TEXT("<<"),
		TEXT("<="),
		TEXT("<"),
		TEXT(">>="),
		TEXT(">>"),
		TEXT(">="),
		TEXT(">"),
		TEXT("=="),
		TEXT("&&"),
		TEXT("&"),
		TEXT("^="),
		TEXT("^"),
		TEXT("|="),
		TEXT("||"),
		TEXT("|"),
		TEXT("?"),
		TEXT("="),

		// chuck operators
		TEXT("++"),
		TEXT("--"),
		TEXT(":"),
		TEXT("+"),
		TEXT("-"),
		TEXT("*"),
		TEXT("/"),
		TEXT("%"),
		TEXT("::"),
		TEXT("=="),
		TEXT("!="),
		TEXT("<"),
		TEXT(">"),
		TEXT("<="),
		TEXT(">="),
		TEXT("&&"),
		TEXT("||"),
		TEXT("&"),
		TEXT("|"),
		TEXT("^"),
		TEXT(">>"),
		TEXT("<<"),
		TEXT("="),
		TEXT("?"),
		TEXT("!"),
		TEXT("~"),
		TEXT("<<<"),
		TEXT(">>>"),
		TEXT("=>"),
		TEXT("!=>"),
		TEXT("=^"),
		TEXT("=v"),
		TEXT("@=>"),
		TEXT("+=>"),
		TEXT("-=>"),
		TEXT("*=>"),
		TEXT("/=>"),
		TEXT("&=>"),
		TEXT("|=>"),
		TEXT("^=>"),
		TEXT(">>=>"),
		TEXT("<<=>"),
		TEXT("%=>"),
		TEXT("@"),
		TEXT("@@"),
		TEXT("->"),
		TEXT("<-"),
		TEXT(".")
	};




	IgnoreClassToRefGenerate = {UUserWidget::StaticClass(), UAnimInstance::StaticClass(), UControlRig::StaticClass() };
}



FString UBlueprintNativizationSettingsLibrary::GetModuleApiName()
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	FString ModuleNameUpper = Settings->ModuleName.ToUpper();
	return ModuleNameUpper + TEXT("_API");
}


FString UBlueprintNativizationSettingsLibrary::GetModuleName()
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	return Settings->ModuleName;
}





bool UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByPropertyNameAndObject(FName PropertyOriginalName, UObject* Object, FGetterAndSetterPropertyDescriptor& OutGetterAndSetterDescriptorDesc)
{
	if (Object == nullptr)
	{
		return false;
	}

	UClass* Class = Object->GetClass();
	if (Class == nullptr)
	{
		return false;
	}

	FProperty* Property = Class->FindPropertyByName(PropertyOriginalName);
	if (Property == nullptr)
	{
		return false;
	}

	return FindGetterAndSetterDescriptorDescriptorByProperty(Property, OutGetterAndSetterDescriptorDesc);
}



bool UBlueprintNativizationSettingsLibrary::FindColorByNameInColorTextGroup(FString Name, FLinearColor& LinearColor)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	for (FColorEditableKeywordGroup ColorEditableKeywordGroup : Settings->ColorEditableTextGroups)
	{
		if (ColorEditableKeywordGroup.Names.Contains(Name))
		{
			LinearColor = ColorEditableKeywordGroup.Color;
			return true;
		}
	}
	return false;
}
