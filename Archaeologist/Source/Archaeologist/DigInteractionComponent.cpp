// 挖掘交互组件的实现 —— 框架 + 实现指引。
//
// 这个文件的量很小（三个函数），但它决定了"玩家到底挖到哪儿"，
// 建议先把 GetViewPoint 和 TryDigFromRay 写对，再写 TryDig。

#include "DigInteractionComponent.h"

#include "DigSite.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UDigInteractionComponent::UDigInteractionComponent()
{
    // 这个组件不需要 Tick，只在玩家点鼠标时才做事。
    PrimaryComponentTick.bCanEverTick = false;
}

// ===========================================================================
// 【1】TryDig：把"玩家视角"变成一条射线
// ===========================================================================
EDigOutcome UDigInteractionComponent::TryDig()
{
    // 目标：取玩家当前看向的位置和方向，算出射线起点/终点，交给 TryDigFromRay。

    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;

    if (!GetViewPoint(ViewLocation, ViewRotation))
    {
        OnDigResult(EDigOutcome::Invalid, FVector::ZeroVector, nullptr);
        return EDigOutcome::Invalid;
    }

    const FVector Start = ViewLocation;
    // FRotator::Vector() 就是"这个朝向的前向量"（Forward）。
    const FVector End = ViewLocation + ViewRotation.Vector() * (double)DigDistance;

    return TryDigFromRay(Start, End);
}

EDigOutcome UDigInteractionComponent::TryDigFromRay(FVector Start, FVector End)
{
    // 目标：用球体扫描找土，命中 ADigSite 就调用它的 ApplyDig。
    //DrawDebugLine
    /*
    DrawDebugLine 接受世界指標 InWorld、起點與終點 LineStart/LineEnd、顏色 Color 以及控制持續時間與粗細等參數。其中：

    • Color 可設定如 FColor::Red 或 FColor::Green 等顏色。
    • bPersistentLines 與 LifeTime 用來控制線段是持續顯示、單幀繪製還是維持特定秒數。
    */
    UWorld* World = GetWorld();
    AActor* OwnerActor = GetOwner();
    if (!World)
    {
        OnDigResult(EDigOutcome::Invalid, Start, nullptr);
        return EDigOutcome::Invalid;
    }

    // 扫描逻辑在 ProbeAimFromRay 里，和准星/高亮共用同一份参数。
    FHitResult Hit;
    const bool bHit = ProbeAimFromRay(Start, End, Hit);

    if (bDrawDebug)
    {
        DrawDebugLine(World, Start, bHit ? Hit.ImpactPoint : End,bHit ? FColor::Green : FColor::Red, /*bPersistentLines=*/false, 2.0f, 0, 0.1f);
        
        if (bHit)
        {
            DrawDebugSphere(World, Hit.ImpactPoint, DigRadius, 8, FColor::Yellow,
                /*bPersistentLines=*/false, 2.0f);
        }
    }

    AActor* HitActor = Hit.GetActor();
    if (!bHit || !HitActor)
    {
        LastHitDigSite = nullptr;
        OnDigResult(EDigOutcome::NoBlockHit, End, nullptr);
        return EDigOutcome::NoBlockHit;
    }

    ADigSite* Site = Cast<ADigSite>(HitActor);
    if (!Site)
    {
        // 打到石头/栅栏/文物：不是土。
        LastHitDigSite = nullptr;
        OnDigResult(EDigOutcome::WrongTarget, Hit.ImpactPoint, HitActor);
        return EDigOutcome::WrongTarget;
    }

    LastHitDigSite = Site;
    const EDigOutcome Outcome = Site->ApplyDig(Hit, OwnerActor);
    OnDigResult(Outcome, Hit.ImpactPoint, HitActor);
    return Outcome;
}

bool UDigInteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    // 目标：给出"玩家此刻真正看到的位置和朝向"。
    //   这个项目有两台相机（Camera1 站立 / Camera2 蹲下），按 C 切换，
    //   所以不能自己按 BaseEyeHeight 算，必须问 PlayerCameraManager 现在用的是哪台。

    const AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return false;
    }

    if (const APawn* Pawn = Cast<APawn>(OwnerActor))
    {
        if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
        {
            if (PC->PlayerCameraManager)
            {
                OutLocation = PC->PlayerCameraManager->GetCameraLocation();
                OutRotation = PC->PlayerCameraManager->GetCameraRotation();
                return true;
            }
        }
    }

    // 兜底：AI 控制、编辑器预览、还没有 Controller 的时候。
    OwnerActor->GetActorEyesViewPoint(OutLocation, OutRotation);
    return true;
}

bool UDigInteractionComponent::ProbeAim(FHitResult& OutHit) const
{
    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    if (!GetViewPoint(ViewLocation, ViewRotation))
    {
        return false;
    }

    const FVector End = ViewLocation + ViewRotation.Vector() * (double)DigDistance;
    return ProbeAimFromRay(ViewLocation, End, OutHit);
}

bool UDigInteractionComponent::ProbeAimFromRay(FVector Start, FVector End, FHitResult& OutHit) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    // 忽略 Owner 自己（玩家胶囊体挡在相机前面，不忽略就永远只打到自己的胶囊）。
    FCollisionQueryParams QueryParams(TEXT("DigAim"), /*bTraceComplex=*/false, GetOwner());
    for (const TObjectPtr<AActor>& Ignored : ExtraIgnoredActors)
    {
        if (Ignored)
        {
            QueryParams.AddIgnoredActor(Ignored);
        }
    }

    return World->SweepSingleByChannel(
        OutHit,
        Start,
        End,
        FQuat::Identity,
        TraceChannel,
        FCollisionShape::MakeSphere(DigRadius),
        QueryParams);
}