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
    //
    // 步骤：
    //   1) FVector ViewLocation; FRotator ViewRotation;
    //      if (!GetViewPoint(ViewLocation, ViewRotation))
    //          -> OnDigResult(EDigOutcome::Invalid, FVector::ZeroVector, nullptr);
    //             return EDigOutcome::Invalid;
    //   2) Start = ViewLocation
    //      End   = ViewLocation + ViewRotation.Vector() * DigDistance
    //      （FRotator::Vector() 取的是"这个朝向的前向量"，就是 UE 里的 Forward）
    //   3) return TryDigFromRay(Start, End);
    return EDigOutcome::Invalid;
}

// ===========================================================================
// 【2】TryDigFromRay：真正做扫描 + 分发
// ===========================================================================
EDigOutcome UDigInteractionComponent::TryDigFromRay(FVector Start, FVector End)
{
    // 目标：用球体扫描找土，命中 ADigSite 就调用它的 ApplyDig。
    //
    // 会用到的引擎函数：
    //   GetWorld()->SweepSingleByChannel(
    //       Hit, Start, End, FQuat::Identity,
    //       TraceChannel,                       // ECollisionChannel
    //       FCollisionShape::MakeSphere(DigRadius),
    //       QueryParams)
    //   返回 bool（有没有命中），命中信息写进 FHitResult Hit。
    //
    //   FCollisionQueryParams QueryParams(TEXT("DigSwing"), /*bTraceComplex=*/false, Owner);
    //   QueryParams.AddIgnoredActor(Actor);    // 遍历 ExtraIgnoredActors 逐个加
    //
    //   DrawDebugLine / DrawDebugSphere        // bDrawDebug 时画出来
    //   Hit.GetActor() / Hit.ImpactPoint
    //   Cast<ADigSite>(Hit.GetActor())
    //
    // 步骤：
    //   1) 取 World 和 Owner，任一为空 -> OnDigResult(Invalid, ...) 并返回 Invalid。
    //   2) 组 QueryParams：忽略 Owner 自己 + ExtraIgnoredActors 里的所有 Actor。
    //      （为什么要忽略：玩家身上有胶囊体，球体扫描从相机往前打，很容易先打到自己的胶囊）
    //   3) SweepSingleByChannel。
    //   4) bDrawDebug 时画线（命中画绿、没命中画红）+ 命中点画个球，方便调参。
    //   5) 没命中 / Hit.GetActor() 为空：
    //        LastHitDigSite = nullptr;
    //        OnDigResult(EDigOutcome::NoBlockHit, End, nullptr);
    //        return NoBlockHit;
    //   6) Cast<ADigSite>：失败说明打到石头/栅栏/文物 ->
    //        OnDigResult(EDigOutcome::WrongTarget, Hit.ImpactPoint, Hit.GetActor());
    //        return WrongTarget;
    //   7) 成功：
    //        LastHitDigSite = Site;
    //        Outcome = Site->ApplyDig(Hit, Owner);
    //        OnDigResult(Outcome, Hit.ImpactPoint, Hit.GetActor());
    //        return Outcome;

    return EDigOutcome::Invalid;
}

// ===========================================================================
// 【3】GetViewPoint：到底以谁的眼睛为准
// ===========================================================================
bool UDigInteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    // 目标：给出"玩家此刻真正看到的位置和朝向"。
    //
    // 这个项目的特殊之处：角色有两台相机（Camera1 站立 / Camera2 蹲下），
    // 按 C 键来回切换。所以**不能**用 Pawn 的 BaseEyeHeight 自己算，
    // 必须问引擎"现在激活的是哪台相机"。
    //
    // 步骤：
    //   1) const AActor* Owner = GetOwner(); if (!Owner) return false;
    //   2) 尝试走相机：
    //        const APawn* Pawn = Cast<APawn>(Owner);
    //        const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    //        if (PC && PC->PlayerCameraManager)
    //        {
    //            OutLocation = PC->PlayerCameraManager->GetCameraLocation();
    //            OutRotation = PC->PlayerCameraManager->GetCameraRotation();
    //            return true;
    //        }
    //      注意：PC 可能是 nullptr（AI 控制、或者还没有 Controller），每一层都要判空。
    //   3) 兜底（AI / 编辑器预览）：
    //        Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
    //        return true;
    return false;
}
