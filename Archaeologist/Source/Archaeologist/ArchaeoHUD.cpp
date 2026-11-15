// 准星 HUD 的实现。

#include "ArchaeoHUD.h"

#include "AimAssistComponent.h"

#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AArchaeoHUD::AArchaeoHUD()
{
    // 每帧画，不需要 Tick 以外的逻辑。
    PrimaryActorTick.bCanEverTick = false;
}

void AArchaeoHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
    {
        return;
    }

    const UAimAssistComponent* Aim = FindAimAssist();

    const FLinearColor Color = Aim ? Aim->GetAimColor() : FLinearColor(1.0f, 1.0f, 1.0f, 0.55f);
    const bool bHasTarget = Aim && Aim->HasAimTarget();

    DrawCrosshair(Color, bHasTarget);

    if (bDrawProgressArc && Aim)
    {
        DrawProgressArc(Color, Aim->GetAimProgress());
    }
}

UAimAssistComponent* AArchaeoHUD::FindAimAssist() const
{
    const APlayerController* PC = GetOwningPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;

    return Pawn ? Pawn->FindComponentByClass<UAimAssistComponent>() : nullptr;
}

void AArchaeoHUD::DrawCrosshair(const FLinearColor& Color, bool bHasTarget)
{
    const float CX = Canvas->SizeX * 0.5f;
    const float CY = Canvas->SizeY * 0.5f;

    // 命中目标时四条线往里收，给一点"咬住"的反馈。
    const float Gap = bHasTarget ? CrosshairGap * 0.6f : CrosshairGap;
    const float Len = CrosshairLength;

    DrawLine(CX - Gap, CY, CX - Gap - Len, CY, Color, CrosshairThickness);
    DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Color, CrosshairThickness);
    DrawLine(CX, CY - Gap, CX, CY - Gap - Len, Color, CrosshairThickness);
    DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Color, CrosshairThickness);

    if (DotRadius > 0.0f)
    {
        DrawRect(Color, CX - DotRadius, CY - DotRadius, DotRadius * 2.0f, DotRadius * 2.0f);
    }
}

void AArchaeoHUD::DrawProgressArc(const FLinearColor& Color, float Progress)
{
    if (Progress <= 0.0f)
    {
        return;
    }

    const float CX = Canvas->SizeX * 0.5f;
    const float CY = Canvas->SizeY * 0.5f;

    const int32 Segments = 32;
    const int32 Filled = FMath::Clamp(FMath::RoundToInt(Segments * Progress), 0, Segments);
    if (Filled <= 0)
    {
        return;
    }

    // 从正上方开始顺时针画。
    FVector2D Prev(CX, CY - ArcRadius);
    for (int32 i = 1; i <= Filled; ++i)
    {
        const float Angle = -HALF_PI + 2.0f * PI * (float)i / (float)Segments;
        const FVector2D P(CX + FMath::Cos(Angle) * ArcRadius, CY + FMath::Sin(Angle) * ArcRadius);

        DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, CrosshairThickness * 2.0f);
        Prev = P;
    }
}
