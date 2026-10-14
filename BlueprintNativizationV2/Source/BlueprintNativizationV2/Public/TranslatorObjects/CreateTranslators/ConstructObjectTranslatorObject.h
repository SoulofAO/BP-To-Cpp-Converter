/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_GenericCreateObject.h"
#include "ConstructObjectTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UConstructObjectTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
    UConstructObjectTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_GenericCreateObject::StaticClass() };
        NodeNamesForCheck = { };
    }

    virtual bool ShouldExpandNode(UK2Node* Node) const override
    {
        return true;
    }

};
