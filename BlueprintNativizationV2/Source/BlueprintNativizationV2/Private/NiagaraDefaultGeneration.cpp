#include "NiagaraDefaultGeneration.h"
#include "NiagaraComponent.h"
#include "NiagaraVariant.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "RetainedBlueprintGeneration.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "UObject/UnrealType.h"

FString FNiagaraDefaultGeneration::Generate(UNiagaraComponent* Component, const FString& Target)
{
    FString Code;
    auto FormatComponents = [](const FString& TypeName, const TArray<double>& Components)
    {
        TArray<FString> Arguments;
        for (double ComponentValue : Components)
        {
            Arguments.Add(FString::SanitizeFloat(ComponentValue));
        }
        return TypeName + TEXT("(") + FString::Join(Arguments, TEXT(", ")) + TEXT(")");
    };
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes.Add(TEXT("NiagaraComponent.h"));
    Requirements.ModuleDependencies.Add(TEXT("Niagara"));
    FNativeGenerationContext::AddRequirements(Requirements);
    for (const FName PropertyName : {FName(TEXT("TemplateParameterOverrides")), FName(TEXT("InstanceParameterOverrides"))})
    {
        FMapProperty* Overrides = FindFProperty<FMapProperty>(UNiagaraComponent::StaticClass(), PropertyName);
        FScriptMapHelper Values(Overrides, Overrides->ContainerPtrToValuePtr<void>(Component));
        for (int32 EntryIndex = 0; EntryIndex < Values.GetMaxIndex(); ++EntryIndex)
        {
            if (!Values.IsValidIndex(EntryIndex))
            {
                continue;
            }
            const FNiagaraVariableBase& Variable = *reinterpret_cast<const FNiagaraVariableBase*>(Values.GetKeyPtr(EntryIndex));
            const FNiagaraVariant& Value = *reinterpret_cast<const FNiagaraVariant*>(Values.GetValuePtr(EntryIndex));
            const FNiagaraTypeDefinition& Type = Variable.GetType();
            FString Method;
            FString Expression;
            if (Value.GetMode() == ENiagaraVariantMode::Bytes && Value.IsValid(Type))
            {
                if (Type == FNiagaraTypeDefinition::GetFloatDef())
                {
                    Method = TEXT("SetVariableFloat");
                    Expression = FString::SanitizeFloat(Value.GetBytesValue<float>(Type)) + TEXT("f");
                }
                else if (Type == FNiagaraTypeDefinition::GetIntDef())
                {
                    Method = TEXT("SetVariableInt");
                    Expression = FString::FromInt(Value.GetBytesValue<int32>(Type));
                }
                else if (Type == FNiagaraTypeDefinition::GetBoolDef())
                {
                    Method = TEXT("SetVariableBool");
                    Expression = TEXT("false");
                    if (Value.GetBytesValue<FNiagaraBool>(Type).GetValue())
                    {
                        Expression = TEXT("true");
                    }
                }
                else if (Type == FNiagaraTypeDefinition::GetColorDef())
                {
                    const FLinearColor Color = Value.GetBytesValue<FLinearColor>(Type);
                    Method = TEXT("SetVariableLinearColor");
                    Expression = FormatComponents(TEXT("FLinearColor"), {Color.R, Color.G, Color.B, Color.A});
                }
                else if (Type == FNiagaraTypeDefinition::GetVec2Def())
                {
                    const FVector2f Vector = Value.GetBytesValue<FVector2f>(Type);
                    Method = TEXT("SetVariableVec2");
                    Expression = FormatComponents(TEXT("FVector2D"), {Vector.X, Vector.Y});
                }
                else if (Type == FNiagaraTypeDefinition::GetVec3Def())
                {
                    const FVector3f Vector = Value.GetBytesValue<FVector3f>(Type);
                    Method = TEXT("SetVariableVec3");
                    Expression = FormatComponents(TEXT("FVector"), {Vector.X, Vector.Y, Vector.Z});
                }
                else if (Type == FNiagaraTypeDefinition::GetPositionDef())
                {
                    const FNiagaraPosition Position = Value.GetBytesValue<FNiagaraPosition>(Type);
                    Method = TEXT("SetVariablePosition");
                    Expression = FormatComponents(TEXT("FVector"), {Position.X, Position.Y, Position.Z});
                }
                else if (Type == FNiagaraTypeDefinition::GetVec4Def())
                {
                    const FVector4f Vector = Value.GetBytesValue<FVector4f>(Type);
                    Method = TEXT("SetVariableVec4");
                    Expression = FormatComponents(TEXT("FVector4"), {Vector.X, Vector.Y, Vector.Z, Vector.W});
                }
                else if (Type == FNiagaraTypeDefinition::GetQuatDef())
                {
                    const FQuat4f Rotation = Value.GetBytesValue<FQuat4f>(Type);
                    Method = TEXT("SetVariableQuat");
                    Expression = FormatComponents(TEXT("FQuat"), {Rotation.X, Rotation.Y, Rotation.Z, Rotation.W});
                }
            }
            if (Method.IsEmpty())
            {
                FNativeApiCatalog::AddError(TEXT("Unsupported Niagara default parameter: ") + Component->GetPathName() + TEXT(".") + Variable.GetName().ToString());
                continue;
            }
            Code += Target + TEXT("->") + Method + TEXT("(FName(") + FRetainedBlueprintGeneration::Quote(Variable.GetName().ToString()) + TEXT("), ") + Expression + TEXT(");\n");
        }
    }
    return Code;
}
