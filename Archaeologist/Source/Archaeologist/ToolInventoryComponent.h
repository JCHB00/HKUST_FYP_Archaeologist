// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ToolData.h"
#include "ToolInventoryComponent.generated.h"

class AToolBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToolChanged, const FToolData&, NewToolData);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARCHAEOLOGIST_API UToolInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UToolInventoryComponent();

    // 初始化工具（用於 DataTable 自動生成，如果你要用這套）
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void InitializeTools(const TArray<FToolData>& InTools);

    // 【新增】把地圖上撿到的實體工具加入背包
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void AddExistingTool(AToolBase* NewTool);

    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void CycleTool();

    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void SelectToolByIndex(int32 Index);

    UFUNCTION(BlueprintPure, Category = "Inventory")
    FToolData GetCurrentToolData() const;

    UFUNCTION(BlueprintPure, Category = "Inventory")
    AToolBase* GetCurrentToolActor() const;

    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnToolChanged OnToolChanged;

	// for dynamic inventory UI
    UFUNCTION(BlueprintPure, Category = "Inventory")
    int32 GetToolCount() const { return ToolActors.Num(); }

private:
    UPROPERTY() TArray<FToolData> AvailableTools;
    UPROPERTY() TArray<AToolBase*> ToolActors;
    int32 CurrentToolIndex = -1;

    void EquipToolByIndex(int32 Index);
    FName GetHandSocketName() const;
};
