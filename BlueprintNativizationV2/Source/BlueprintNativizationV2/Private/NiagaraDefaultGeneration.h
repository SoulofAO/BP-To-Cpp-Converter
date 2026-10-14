#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;

struct FNiagaraDefaultGeneration
{
    static FString Generate(UNiagaraComponent* Component, const FString& Target);
};
