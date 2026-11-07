// Fill out your copyright notice in the Description page of Project Settings.


#include "ToolBase.h"
#include "Components/StaticMeshComponent.h" // 補上這個標頭檔

AToolBase::AToolBase()
{
    PrimaryActorTick.bCanEverTick = false;

    ToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolMesh"));
    RootComponent = ToolMesh;

    //ToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // 允許被射線或碰撞查詢偵測到，但關閉物理推擠
    ToolMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ToolMesh->SetCollisionResponseToAllChannels(ECR_Block);

    // 預設不模擬物理，以免掉到地上
    ToolMesh->SetSimulatePhysics(false);
}

void AToolBase::OnEquip()
{
    SetActorHiddenInGame(false);

    // 裝備時關閉碰撞，避免卡住玩家
    ToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AToolBase::OnUnequip()
{
    SetActorHiddenInGame(true);
}

