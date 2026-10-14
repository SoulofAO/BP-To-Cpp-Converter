// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_AIMoveTo.h"
#include "AIMoveToTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UAIMoveToTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:

    UAIMoveToTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_AIMoveTo::StaticClass() };
        NodeNamesForCheck = { };
    }

    virtual bool ShouldExpandNode(UK2Node* Node) const override;

};
