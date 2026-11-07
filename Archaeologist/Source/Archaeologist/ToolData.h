// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ToolData.generated.h"

class AToolBase;

USTRUCT(BlueprintType)
struct FToolData : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName ToolID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText ToolName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    UTexture2D* ToolIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AToolBase> ToolClass;
};