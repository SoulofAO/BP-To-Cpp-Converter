/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "BlueprintNativizationData.h"

class UNativizationV2Subsystem;

FGenerateResultStruct GenerateMulticastDelegateStatement(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem);
