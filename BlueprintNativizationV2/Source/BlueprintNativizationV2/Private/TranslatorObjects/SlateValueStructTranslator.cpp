/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "BlueprintNativizationSettings.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/StyleColors.h"
#include "StructInitialization.h"
#include "StructPrivateMemberAccess.h"
#include "UObject/UnrealType.h"

struct FSlateColorTableAccess
{
    using Member = EStyleColor FSlateColor::*;
    friend Member GetStructPrivateMember(FSlateColorTableAccess);
};
template struct TStructPrivateMemberAccess<FSlateColorTableAccess, &FSlateColor::ColorTableId>;

static FString GenerateSlateColor(const FSlateColor& Value, FString& Error)
{
    const auto* Rule = FindFProperty<FEnumProperty>(FSlateColor::StaticStruct(), TEXT("ColorUseRule"));
    const auto Mode = static_cast<ESlateColorStylingMode>(Rule->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Rule->ContainerPtrToValuePtr<void>(&Value)));
    switch (Mode)
    {
    case ESlateColorStylingMode::UseColor_Specified:
    {
        const FLinearColor Color = Value.GetSpecifiedColor();
        return FString::Format(TEXT("FSlateColor(FLinearColor({0}, {1}, {2}, {3}))"),
            {FStructInitialization::Number(Color.R), FStructInitialization::Number(Color.G), FStructInitialization::Number(Color.B), FStructInitialization::Number(Color.A)});
    }
    case ESlateColorStylingMode::UseColor_Foreground:
        return TEXT("FSlateColor::UseForeground()");
    case ESlateColorStylingMode::UseColor_Foreground_Subdued:
        return TEXT("FSlateColor::UseSubduedForeground()");
    case ESlateColorStylingMode::UseColor_UseStyle:
        return TEXT("FSlateColor::UseStyle()");
    case ESlateColorStylingMode::UseColor_ColorTable:
    {
        const EStyleColor Table = Value.*GetStructPrivateMember(FSlateColorTableAccess{});
        if (Table >= EStyleColor::MAX)
        {
            Error = TEXT("Invalid Slate color table entry.");
            return FString();
        }
        return TEXT("FSlateColor(EStyleColor::") + StaticEnum<EStyleColor>()->GetNameStringByValue(static_cast<int64>(Table)) + TEXT(")");
    }
    default:
        Error = TEXT("Unsupported Slate color styling mode.");
        return FString();
    }
}

bool USlateValueStructTranslator::CanTranslate(const UScriptStruct* Structure) const
{
    return Structure == FSlateColor::StaticStruct() || Structure == FSlateBrush::StaticStruct();
}

bool USlateValueStructTranslator::TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    if (Request.Structure == FSlateColor::StaticStruct())
    {
        const auto* Rule = FindFProperty<FEnumProperty>(Request.Structure, TEXT("ColorUseRule"));
        if (Rule->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Rule->ContainerPtrToValuePtr<void>(Source)) ==
            static_cast<uint64>(ESlateColorStylingMode::UseColor_ColorTable))
        {
            Result.Error = TEXT("Slate color table identity is not serialized by ImportText.");
            return false;
        }
    }
    else
    {
        const auto& Brush = *static_cast<const FSlateBrush*>(Source);
        if (!Brush.IsSet())
        {
            Result.Error = TEXT("Optional brush state is not serialized by ImportText.");
            return false;
        }
        const auto* Rule = FindFProperty<FEnumProperty>(FSlateColor::StaticStruct(), TEXT("ColorUseRule"));
        for (const FSlateColor* Color : {&Brush.TintColor, &Brush.OutlineSettings.Color})
        {
            if (Rule->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Rule->ContainerPtrToValuePtr<void>(Color)) ==
                static_cast<uint64>(ESlateColorStylingMode::UseColor_ColorTable))
            {
                Result.Error = TEXT("Brush color table identity is not serialized by ImportText.");
                return false;
            }
        }
    }
    return FStructInitialization::Fallback(Request, Result);
}

bool USlateValueStructTranslator::Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    const bool bColor = Request.Structure == FSlateColor::StaticStruct();
    FString NativeType = TEXT("FSlateBrush");
    FString Header = TEXT("Styling/SlateBrush.h");
    if (bColor)
    {
        NativeType = TEXT("FSlateColor");
        Header = TEXT("Styling/SlateColor.h");
    }
    if (FStructInitialization::Describe(Request, Result, NativeType, Header, TEXT("SlateCore")))
    {
        return true;
    }
    if (!FStructInitialization::IsValueOperation(Request.Operation))
    {
        if (!bColor && Request.Operation == EStructTranslationOperation::ReadMember)
        {
            FProperty* Member = Request.Structure->FindPropertyByName(Request.MemberName);
            if (Member && Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic))
            {
                Result.Expression = Request.SourceExpression + TEXT(".") + Member->GetNameCPP();
                return true;
            }
        }
        Result.Error = TEXT("Unsupported Slate structure operation.");
        return false;
    }
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    Result.HeaderIncludes.Add(TEXT("Styling/StyleColors.h"));
    if (bColor)
    {
        Result.Expression = GenerateSlateColor(*static_cast<const FSlateColor*>(Source), Result.Error);
        return !Result.Expression.IsEmpty();
    }
    const auto& Value = *static_cast<const FSlateBrush*>(Source);
    if (Value.IsDynamicallyLoaded() || !Value.IsSet())
    {
        Result.Error = TEXT("Dynamic or optional brush requires a complete serialized initializer.");
        return false;
    }
    const FString Name = FStructInitialization::VariableName(Request, TEXT("BrushSettings"));
    const FName ResourceName = FindFProperty<FNameProperty>(Request.Structure, TEXT("ResourceName"))->GetPropertyValue_InContainer(Source);
    FSlateBrush Baseline;
    if (!ResourceName.IsNone())
    {
        Result.HeaderIncludes.Add(TEXT("Brushes/SlateImageBrush.h"));
        Result.Preparations.Add(TEXT("FSlateBrush ") + Name + TEXT(" = FSlateImageBrush(FName(") +
            FStructInitialization::Quote(ResourceName.ToString()) + TEXT("), FVector2D(32.0, 32.0));"));
        Baseline = FSlateImageBrush(ResourceName, FVector2D(32.0, 32.0));
    }
    else
    {
        Result.Preparations.Add(TEXT("FSlateBrush ") + Name + TEXT(";"));
    }
    for (TFieldIterator<FProperty> Field(Request.Structure); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient) || Field->GetFName() == TEXT("ResourceName") ||
            Field->GetFName() == TEXT("ResourceObject") || Field->GetFName() == TEXT("UVRegion") ||
            Field->GetFName() == TEXT("bIsDynamicallyLoaded"))
        {
            continue;
        }
        const void* ValueAddress = Field->ContainerPtrToValuePtr<void>(Source);
        if (Field->Identical(ValueAddress, Field->ContainerPtrToValuePtr<void>(&Baseline), PPF_None))
        {
            continue;
        }
        if (Field->GetFName() == TEXT("ImageSize"))
        {
            const FVector2f Size = Value.GetImageSize();
            Result.Preparations.Add(FString::Format(TEXT("{0}.SetImageSize(FVector2f({1}, {2}));"),
                {Name, FStructInitialization::Number(Size.X), FStructInitialization::Number(Size.Y)}));
            continue;
        }
        if (!Field->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic))
        {
            Result.Error = TEXT("Unsupported brush setting: ") + Field->GetName();
            return false;
        }
        Result.Preparations.Add(Name + TEXT(".") + Field->GetNameCPP() + TEXT(" = ") +
            FStructInitialization::PropertyExpression(*Field, ValueAddress, Request) + TEXT(";"));
    }
    const FBox2f Region = Value.GetUVRegion();
    if (Region.bIsValid)
    {
        Result.Preparations.Add(FString::Format(TEXT("{0}.SetUVRegion(FBox2f(FVector2f({1}, {2}), FVector2f({3}, {4})));"),
            {Name, FStructInitialization::Number(Region.Min.X), FStructInitialization::Number(Region.Min.Y),
                FStructInitialization::Number(Region.Max.X), FStructInitialization::Number(Region.Max.Y)}));
    }
    if (Value.GetResourceObject())
    {
        auto* Resource = FindFProperty<FObjectPropertyBase>(Request.Structure, TEXT("ResourceObject"));
        const FString Expression = FStructInitialization::PropertyExpression(Resource, Resource->ContainerPtrToValuePtr<void>(Source), Request);

        Result.Preparations.Add(Name + TEXT(".SetResourceObject(") + Expression + TEXT(");"));
    }
    Result.Expression = Name;
    return true;
}
