/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;

struct FNiagaraDefaultGeneration
{
    static FString Generate(UNiagaraComponent* Component, const FString& Target);
};
