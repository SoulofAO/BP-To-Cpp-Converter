/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "GameplayTagContainer.h"
#include "StructInitialization.h"
#include "UObject/UnrealType.h"

static bool GenerateQueryExpression(const FGameplayTagQueryExpression& Expression,
    const FStructTranslationRequest& Request, FStructTranslationResult& Result, FString& Name)
{
    FString Operation;
    switch (Expression.ExprType)
    {
    case EGameplayTagQueryExprType::AnyTagsMatch: Operation = TEXT("AnyTagsMatch"); break;
    case EGameplayTagQueryExprType::AllTagsMatch: Operation = TEXT("AllTagsMatch"); break;
    case EGameplayTagQueryExprType::NoTagsMatch: Operation = TEXT("NoTagsMatch"); break;
    case EGameplayTagQueryExprType::AnyTagsExactMatch: Operation = TEXT("AnyTagsExactMatch"); break;
    case EGameplayTagQueryExprType::AllTagsExactMatch: Operation = TEXT("AllTagsExactMatch"); break;
    case EGameplayTagQueryExprType::AnyExprMatch: Operation = TEXT("AnyExprMatch"); break;
    case EGameplayTagQueryExprType::AllExprMatch: Operation = TEXT("AllExprMatch"); break;
    case EGameplayTagQueryExprType::NoExprMatch: Operation = TEXT("NoExprMatch"); break;
    default:
        Result.Error = TEXT("Unsupported gameplay tag query expression.");
        return false;
    }
    if ((Expression.UsesTagSet() && !Expression.ExprSet.IsEmpty()) ||
        (Expression.UsesExprSet() && !Expression.TagSet.IsEmpty()))
    {
        Result.Error = TEXT("Gameplay tag query contains incompatible expression data.");
        return false;
    }
    Name = FStructInitialization::VariableName(Request, TEXT("Query") + Operation);
    Result.Preparations.Add(TEXT("FGameplayTagQueryExpression ") + Name + TEXT(";"));
    Result.Preparations.Add(Name + TEXT(".") + Operation + TEXT("();"));
    for (const FGameplayTag& Tag : Expression.TagSet)
    {
        if (!Tag.IsValid())
        {
            Result.Error = TEXT("Gameplay tag query contains an invalid tag.");
            return false;
        }
        Result.Preparations.Add(Name + TEXT(".AddTag(FGameplayTag::RequestGameplayTag(FName(") +
            FStructInitialization::Quote(Tag.GetTagName().ToString()) + TEXT(")));"));
    }
    for (const FGameplayTagQueryExpression& Child : Expression.ExprSet)
    {
        FString ChildName;
        if (!GenerateQueryExpression(Child, Request, Result, ChildName))
        {
            return false;
        }
        Result.Preparations.Add(Name + TEXT(".AddExpr(") + ChildName + TEXT(");"));
    }
    return true;
}

bool UGameplayTagQueryStructTranslator::CanTranslate(const UScriptStruct* Structure) const
{
    return Structure == FGameplayTagQuery::StaticStruct();
}

bool UGameplayTagQueryStructTranslator::Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    if (FStructInitialization::Describe(Request, Result, TEXT("FGameplayTagQuery"), TEXT("GameplayTagContainer.h"), TEXT("GameplayTags")))
    {
        return true;
    }
    if (!FStructInitialization::IsValueOperation(Request.Operation))
    {
        Result.Error = TEXT("Unsupported gameplay tag query operation.");
        return false;
    }
    FStructOnScope Storage(Request.Structure);
    const auto* Value = static_cast<const FGameplayTagQuery*>(FStructInitialization::ReadValue(Request, Storage, Result.Error));
    if (!Value)
    {
        return false;
    }
    const FString Description = FindFProperty<FStrProperty>(Request.Structure, TEXT("UserDescription"))->GetPropertyValue_InContainer(Value);
    if (Value->IsEmpty())
    {
        Result.Expression = TEXT("FGameplayTagQuery()");
        if (!Description.IsEmpty())
        {
            const FString Name = FStructInitialization::VariableName(Request, TEXT("EmptyTagQuery"));
            Result.Preparations.Add(TEXT("FGameplayTagQuery ") + Name + TEXT(";"));
            Result.Preparations.Add(Name + TEXT(".SetUserDescription(") + FStructInitialization::Quote(Description) + TEXT(");"));
            Result.Expression = Name;
        }
        return true;
    }
    FGameplayTagQueryExpression Root{};
    Value->GetQueryExpr(Root);
    FString RootName;
    if (!GenerateQueryExpression(Root, Request, Result, RootName))
    {
        return false;
    }
    Result.Expression = TEXT("FGameplayTagQuery::BuildQuery(") + RootName + TEXT(", ") +
        FStructInitialization::Quote(Description) + TEXT(")");
    return true;
}
