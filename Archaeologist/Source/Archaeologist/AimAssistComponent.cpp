// 瞄准辅助组件的实现。

#include "AimAssistComponent.h"

#include "DigInteractionComponent.h"
#include "DigSite.h"

#include "Components/LineBatchComponent.h"
#include "Engine/World.h"

UAimAssistComponent::UAimAssistComponent()
{
    // 只需要"定时扫一眼"，所以自己控制 Tick 节奏，不跟帧率绑定。
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UAimAssistComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* OwnerActor = GetOwner())
    {
        DigInteraction = OwnerActor->FindComponentByClass<UDigInteractionComponent>();
    }

    EnsureLineBatch();
}

void UAimAssistComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AimLines)
    {
        AimLines->Flush();
    }

    Super::EndPlay(EndPlayReason);
}

void UAimAssistComponent::EnsureLineBatch()
{
    UWorld* World = GetWorld();
    AActor* OwnerActor = GetOwner();
    if (!World || !OwnerActor || AimLines)
    {
        return;
    }

    // 线条批处理组件：运行时创建，不占用任何资产，打包后同样有效。
    AimLines = NewObject<ULineBatchComponent>(OwnerActor, TEXT("AimAssistLines"));
    AimLines->bCalculateAccurateBounds = false;
    AimLines->SetMobility(EComponentMobility::Movable);
    AimLines->RegisterComponentWithWorld(World);
}

void UAimAssistComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TimeAccumulator += DeltaTime;
    if (TimeAccumulator < AimUpdateInterval)
    {
        return;
    }
    TimeAccumulator = 0.0f;

    UpdateAim();
}

void UAimAssistComponent::UpdateAim()
{
    if (!DigInteraction)
    {
        if (AActor* OwnerActor = GetOwner())
        {
            DigInteraction = OwnerActor->FindComponentByClass<UDigInteractionComponent>();
        }
    }

    EnsureLineBatch();

    FHitResult Hit;
    const bool bHit = DigInteraction && DigInteraction->ProbeAim(Hit);

    ADigSite* Site = bHit ? Cast<ADigSite>(Hit.GetActor()) : nullptr;

    FDigCellCoord Cell;
    bool bHasCell = false;
    float Progress = 0.0f;
    EDigAimState NewState = EDigAimState::NoTarget;

    if (bHit)
    {
        AimHitLocation = Hit.ImpactPoint;

        if (!Site)
        {
            NewState = EDigAimState::WrongTarget;
        }
        else
        {
            bHasCell = Site->ResolveCellFromHit(Hit, Cell);
            if (!bHasCell || !Site->CanDigCell(Cell))
            {
                NewState = EDigAimState::Blocked;
            }
            else
            {
                NewState = EDigAimState::Diggable;
                Progress = Site->GetCellDigProgress(Cell);
            }
        }
    }
    else
    {
        AimHitLocation = FVector::ZeroVector;
    }

    const bool bChanged = (NewState != AimState) || !FMath::IsNearlyEqual(Progress, AimProgress, 0.001f);

    AimState = NewState;
    AimProgress = Progress;

    DrawVisuals(Hit, bHit, Site, Cell, bHasCell);

    if (bChanged)
    {
        OnAimChanged.Broadcast(AimState, AimHitLocation, Site != nullptr, AimProgress);
    }
}

void UAimAssistComponent::DrawVisuals(const FHitResult& Hit, bool bHit, ADigSite* Site, const FDigCellCoord& Cell, bool bHasCell)
{
    if (!AimLines)
    {
        return;
    }

    // 每帧先清空上一次画的线，避免越积越多。
    AimLines->Flush();

    if (!bHit)
    {
        return;
    }

    const FLinearColor Color = GetAimColor();

    // ---- 目标格子高亮 ----
    // 不画整个立方体：立方体在土块内部，会被深度测试整个挡掉，什么都看不见。
    // 改成把这一格"朝向玩家的那一面"贴在土表面外侧画一个方框。
    if (bDrawCellHighlight && Site && bHasCell)
    {
        const float CellSize = Site->GetCellSize();
        const FVector Center = Site->GetCellCenterWorld(Cell);
        DrawCellFace(Center, Hit.ImpactPoint, Hit.ImpactNormal, CellSize * 0.46f, Color);
    }

    // ---- 命中点小圆环 ----
    DrawImpactRing(Hit.ImpactPoint, Hit.ImpactNormal, Color);
}

void UAimAssistComponent::DrawCellFace(const FVector& CellCenter, const FVector& ImpactPoint, const FVector& FaceNormal, float HalfSize, const FLinearColor& Color)
{
    if (!AimLines)
    {
        return;
    }

    const FVector N = FaceNormal.GetSafeNormal();

    // 把格心投影到命中那个面上，再往外挪 0.5cm，避免和土块表面深度打架（z-fighting）。
    const FVector OnPlane = CellCenter - ((CellCenter - ImpactPoint) | N) * N;
    const FVector Center = OnPlane + N * 0.5;

    FVector Tangent = FVector::CrossProduct(N, FVector::UpVector);
    if (Tangent.IsNearlyZero())
    {
        Tangent = FVector::CrossProduct(N, FVector::RightVector);
    }
    Tangent.Normalize();
    const FVector Binormal = FVector::CrossProduct(N, Tangent);

    const FVector Corners[4] = {
        Center + (-Tangent - Binormal) * HalfSize,
        Center + (Tangent - Binormal) * HalfSize,
        Center + (Tangent + Binormal) * HalfSize,
        Center + (-Tangent + Binormal) * HalfSize
    };

    for (int32 i = 0; i < 4; ++i)
    {
        AimLines->DrawLine(Corners[i], Corners[(i + 1) % 4], Color, 0, HighlightThickness);
    }
}

void UAimAssistComponent::DrawImpactRing(const FVector& Center, const FVector& Normal, const FLinearColor& Color)
{
    if (!AimLines || ImpactRingRadius <= 0.0f)
    {
        return;
    }

    const FVector N = Normal.GetSafeNormal();
    FVector Tangent = FVector::CrossProduct(N, FVector::UpVector);
    if (Tangent.IsNearlyZero())
    {
        Tangent = FVector::CrossProduct(N, FVector::RightVector);
    }
    Tangent.Normalize();
    const FVector Binormal = FVector::CrossProduct(N, Tangent);

    const int32 Segments = 16;
    FVector Prev = Center + Tangent * ImpactRingRadius;

    for (int32 i = 1; i <= Segments; ++i)
    {
        const float Angle = 2.0f * PI * (float)i / (float)Segments;
        const FVector P = Center
            + (Tangent * FMath::Cos(Angle) + Binormal * FMath::Sin(Angle)) * ImpactRingRadius;

        AimLines->DrawLine(Prev, P, Color, 0, HighlightThickness);
        Prev = P;
    }
}

bool UAimAssistComponent::HasAimTarget() const
{
    return AimState == EDigAimState::Diggable || AimState == EDigAimState::Blocked;
}

FLinearColor UAimAssistComponent::GetAimColor() const
{
    switch (AimState)
    {
    case EDigAimState::Diggable:
        return DiggableColor;
    case EDigAimState::Blocked:
        return BlockedColor;
    case EDigAimState::WrongTarget:
        return WrongTargetColor;
    case EDigAimState::NoTarget:
    default:
        return NoTargetColor;
    }
}
