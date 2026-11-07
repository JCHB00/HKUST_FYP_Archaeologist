// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToolBase.generated.h"

UCLASS(Blueprintable)
class ARCHAEOLOGIST_API AToolBase : public AActor
{
    GENERATED_BODY()

public:
    AToolBase();

    UFUNCTION(BlueprintCallable, Category = "Tool")
    virtual void OnEquip();

    UFUNCTION(BlueprintCallable, Category = "Tool")
    virtual void OnUnequip();

    UFUNCTION(BlueprintImplementableEvent, Category = "Tool")
    void UseTool();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UStaticMeshComponent* ToolMesh;
};
