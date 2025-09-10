/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "ConversionDetailsCustomizations.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/STextComboBox.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/Optional.h"
#include "BlueprintNativizationLibrary.h"

void FConversionDetailsFunctionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	UClass* Class = DetailBuilder.GetBaseClass();
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(TEXT("Conversion"));

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	UK2Node* ContextNode = Cast<UK2Node>(ObjectsBeingCustomized[0]);

	if (!ContextNode)
	{
		if (UEdGraph* Graph = Cast<UEdGraph>(ObjectsBeingCustomized[0]))
		{
			TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetEntryNodesPerGraph(Graph);
			if (Nodes.Num() <= 1)
			{
				if (Nodes[0]->GetBlueprint()->UbergraphPages.Contains(Graph))
				{
					return;
				}
				ContextNode = Nodes[0];
			}
			
		}
	}

	if (!ContextNode)
	{
		return;
	}

	TArray<FString> FlagOptions;

	FlagOptions.Add(UBlueprintNativizationLibrary::ConversionFunctionFlagToString(EConversionFunctionFlag::ConvertToCPP));
	FlagOptions.Add(UBlueprintNativizationLibrary::ConversionFunctionFlagToString(EConversionFunctionFlag::ConvertAsNativeFunction));
	FlagOptions.Add(UBlueprintNativizationLibrary::ConversionFunctionFlagToString(EConversionFunctionFlag::Ignore));

	IDetailLayoutBuilder* BuilderPtr = &DetailBuilder;

	const FText FuncNameText = FText::FromName(ContextNode->GetFName());
	EConversionFunctionFlag CurrentFlag = UBlueprintNativizationLibrary::GetConversionFunctionFlagForFunction(ContextNode);
	FString CurrentFlagString = UBlueprintNativizationLibrary::ConversionFunctionFlagToString(CurrentFlag);

	TSharedPtr<SConversionStringComboBox> Combo;
	TSharedPtr<SButton> Button;

	EditingNode = ContextNode;

	Cat.AddCustomRow(FuncNameText)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("ConvertType")))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SAssignNew(Combo, SConversionStringComboBox)
				.ValueList(FlagOptions)
				.MetaDataValue(*CurrentFlagString)
				.OnSetMetaData_Lambda([ContextNode, BuilderPtr](const FName&, const FString& NewValue)
					{
						EConversionFunctionFlag NewFlag = UBlueprintNativizationLibrary::ConversionFunctionFlagFromString(*NewValue);
						UBlueprintNativizationLibrary::SetConversionFunctionFlagForFunction(ContextNode, NewFlag);
						if (BuilderPtr) BuilderPtr->ForceRefreshDetails();
					})
		];
	Cat.AddCustomRow(FuncNameText)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("TransformOnlyFunctionToCode")))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SAssignNew(Button, SButton)
				.OnClicked(this, &FConversionDetailsFunctionCustomization::OnTransformOnlyFunctionToCodeButtonClicked)
				[
					SNew(STextBlock).Text(FText::FromString("Transform"))
				]
		];
}


FReply FConversionDetailsFunctionCustomization::OnTransformOnlyFunctionToCodeButtonClicked()
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	FName SelectedNodeOriginalFunctionName = UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(EditingNode.Get());

	if (Settings->SetupActionObject)
	{
		Cast<USetupActionObject>(Settings->SetupActionObject->GetDefaultObject())->ActionToNode("TransformOnlyFunctionToCode", SelectedNodeOriginalFunctionName, EditingNode->GetBlueprint());
	}

	return FReply::Handled();
}
 
void  SConversionStringComboBox::Construct(const FArguments& InArgs)
{
	Key = InArgs._Key;
	MetaDataValue = InArgs._MetaDataValue;
	OnSetMetaData = InArgs._OnSetMetaData;

	Algo::Transform(InArgs._ValueList, ValueList, [](const FString& Value)
		{
			return MakeShared<FString>(Value);
		});

	ChildSlot
		[
			SNew(STextComboBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OptionsSource(&ValueList)
				.InitiallySelectedItem(GetCurrentValue())
				.OnSelectionChanged(this, &SConversionStringComboBox::OnSelected)
		];
}

TSharedPtr<FString>  SConversionStringComboBox::GetCurrentValue() const
{
	const TOptional<FString> ValueStringOptional = MetaDataValue.Get(TOptional<FString>());
	if (ValueStringOptional.IsSet())
	{
		const FString& Value = ValueStringOptional.GetValue();
		for (const TSharedPtr<FString>& ValuePtr : ValueList)
		{
			if (ValuePtr.IsValid() && *ValuePtr == Value)
			{
				return ValuePtr;
			}
		}
	}

	return nullptr;
}

void  SConversionStringComboBox::OnSelected(TSharedPtr<FString> ValuePtr, ESelectInfo::Type SelectInfo)
{
	if (ValuePtr.IsValid())
	{
		OnSetMetaData.ExecuteIfBound(Key, *ValuePtr);
	}
}

void FConversionDetailsPropertyCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	UClass* Class = DetailBuilder.GetBaseClass();
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(TEXT("Conversion"));

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(ObjectsBeingCustomized[0]);

	FProperty* Property = PropertyWrapper ? PropertyWrapper->GetProperty() : nullptr;

	if (!PropertyWrapper->IsInBlueprint())
	{
		return;
	}
	UBlueprint* Blueprint = UBlueprintNativizationLibrary::GetBlueprintByProperty(Property);

	TArray<FString> FlagOptions;
	FlagOptions.Add(UBlueprintNativizationLibrary::ConversionPropertyFlagToString(EConversionPropertyFlag::ConvertToCPP));
	FlagOptions.Add(UBlueprintNativizationLibrary::ConversionPropertyFlagToString(EConversionPropertyFlag::Ignore));

	IDetailLayoutBuilder* BuilderPtr = &DetailBuilder;
	const FText FuncNameText = FText::FromName(Property->GetFName());
	EConversionPropertyFlag CurrentFlag = UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(Property, Blueprint);
	FString CurrentFlagString = UBlueprintNativizationLibrary::ConversionPropertyFlagToString(CurrentFlag);

	TSharedPtr<SConversionStringComboBox> Combo;

	const FText ConvertTupeNameText = FText::FromString(TEXT("ConvertType"));
	Cat.AddCustomRow(FuncNameText)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(ConvertTupeNameText)
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SAssignNew(Combo, SConversionStringComboBox)
				.ValueList(FlagOptions)
				.MetaDataValue(*CurrentFlagString)
				.OnSetMetaData_Lambda([Property, Blueprint, BuilderPtr](const FName&, const FString& NewValue)
					{
						EConversionPropertyFlag NewFlag = UBlueprintNativizationLibrary::ConversionPropertyFlagFromString(*NewValue);
						UBlueprintNativizationLibrary::SetConversionPropertyFlagForProperty(Property, Blueprint, NewFlag);
						if (BuilderPtr) BuilderPtr->ForceRefreshDetails();
					})
		];
}
