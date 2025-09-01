/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BlueprintNativizationSettings.h"
#include "IDetailCustomization.h"

class IBlueprintEditor;

class SConversionStringComboBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnSetMetaData, const FName&, const FString& Value);

	SLATE_BEGIN_ARGS(SConversionStringComboBox)
		{
		}
		SLATE_ARGUMENT_DEFAULT(FName, Key) = NAME_None;
		SLATE_ARGUMENT(TArray<FString>, ValueList);
		SLATE_ATTRIBUTE(TOptional<FString>, MetaDataValue);
		SLATE_EVENT(FOnSetMetaData, OnSetMetaData);
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FString> GetCurrentValue() const;
	void OnSelected(TSharedPtr<FString> ValuePtr, ESelectInfo::Type SelectInfo);

	FName Key = NAME_None;
	TAttribute<TOptional<FString>> MetaDataValue;
	FOnSetMetaData OnSetMetaData;

	TArray<TSharedPtr<FString>> ValueList;

};



class FConversionDetailsFunctionCustomization : public IDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
	{
		return MakeShareable(new FConversionDetailsFunctionCustomization(InBlueprintEditor));
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	FReply OnTransformOnlyFunctionToCodeButtonClicked();

	TWeakObjectPtr<UK2Node> EditingNode;

private:
	explicit FConversionDetailsFunctionCustomization(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
		: BlueprintEditorPtr(InBlueprintEditor)
	{
	}

private:
	TWeakPtr<IBlueprintEditor> BlueprintEditorPtr;
};


class FConversionDetailsPropertyCustomization : public IDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
	{
		return MakeShareable(new FConversionDetailsPropertyCustomization(InBlueprintEditor));
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	explicit FConversionDetailsPropertyCustomization(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
		: BlueprintEditorPtr(InBlueprintEditor)
	{
	}

private:
	TWeakPtr<IBlueprintEditor> BlueprintEditorPtr;
};



