// Fill out your copyright notice in the Description page of Project Settings.


#include "ToolInventoryComponent.h"
#include "ToolBase.h"
#include "GameFramework/Character.h"

UToolInventoryComponent::UToolInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UToolInventoryComponent::InitializeTools(const TArray<FToolData>& InTools)
{
    AvailableTools = InTools;
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    for (const FToolData& ToolData : AvailableTools)
    {
        if (ToolData.ToolClass)
        {
            FActorSpawnParameters SpawnParams;
            SpawnParams.Owner = GetOwner();
            AToolBase* NewTool = GetWorld()->SpawnActor<AToolBase>(ToolData.ToolClass, SpawnParams);
            if (NewTool)
            {
                NewTool->AttachToComponent(OwnerCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, GetHandSocketName());
                NewTool->OnUnequip();
                ToolActors.Add(NewTool);
            }
        }
    }
    // if (ToolActors.Num() > 0) SelectToolByIndex(0);  // 自動裝備
}

// 【新增】把地上的實體加入背包
void UToolInventoryComponent::AddExistingTool(AToolBase* NewTool)
{
    if (!NewTool || ToolActors.Contains(NewTool)) return;

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    // 1. 把工具綁定到角色手上
    NewTool->AttachToComponent(OwnerCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, GetHandSocketName());

    // 2. 加入陣列
    ToolActors.Add(NewTool);

    // 3. 如果你有用 DataTable 的 UI，這裡可能需要生成一個假的 ToolData 塞進 AvailableTools
    // 但為了測試，我們直接裝備最後撿到的這把
    SelectToolByIndex(ToolActors.Num() - 1);
}

void UToolInventoryComponent::CycleTool()
{
    if (ToolActors.Num() == 0) return;
    int32 NextIndex = (CurrentToolIndex + 1) % ToolActors.Num();
    SelectToolByIndex(NextIndex);
}

void UToolInventoryComponent::SelectToolByIndex(int32 Index)
{
    if (!ToolActors.IsValidIndex(Index) || Index == CurrentToolIndex) return;
    EquipToolByIndex(Index);
}

void UToolInventoryComponent::EquipToolByIndex(int32 Index)
{
    if (ToolActors.IsValidIndex(CurrentToolIndex))
    {
        ToolActors[CurrentToolIndex]->OnUnequip();
    }
    CurrentToolIndex = Index;
    ToolActors[CurrentToolIndex]->OnEquip();

    // 觸發 UI 更新（如果你有綁定）
    if (AvailableTools.IsValidIndex(CurrentToolIndex))
    {
        OnToolChanged.Broadcast(AvailableTools[CurrentToolIndex]);
    }
}

FToolData UToolInventoryComponent::GetCurrentToolData() const
{
    if (AvailableTools.IsValidIndex(CurrentToolIndex)) return AvailableTools[CurrentToolIndex];
    return FToolData();
}

AToolBase* UToolInventoryComponent::GetCurrentToolActor() const
{
    if (ToolActors.IsValidIndex(CurrentToolIndex)) return ToolActors[CurrentToolIndex];
    return nullptr;
}

FName UToolInventoryComponent::GetHandSocketName() const
{
    // 【確認這裡】必須跟你的 Skeleton 插槽名字一模一樣！
    return FName("RightHandSocket");
}

