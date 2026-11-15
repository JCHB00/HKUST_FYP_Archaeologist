// 准星 HUD：全部用 Canvas 画，不需要任何 UMG 资产，
// 所以打包后也一定存在（不像 DrawDebug 系列在 Shipping 里会被编译掉）。
//
// 状态来源只有一个：玩家身上的 UAimAssistComponent。
// 这里绝对不要自己再打一条射线 —— 那就是"准星绿、按下去打空"的根源。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DigTypes.h"
#include "ArchaeoHUD.generated.h"

class UAimAssistComponent;

UCLASS()
class ARCHAEOLOGIST_API AArchaeoHUD : public AHUD
{
    GENERATED_BODY()

public:
    AArchaeoHUD();

    virtual void DrawHUD() override;

protected:
    /** 准星四条线离中心多远（像素）。命中目标时会收拢。 */
    UPROPERTY(EditAnywhere, Category = "Crosshair")
    float CrosshairGap = 11.0f;

    /** 每条线多长。 */
    UPROPERTY(EditAnywhere, Category = "Crosshair")
    float CrosshairLength = 9.0f;

    UPROPERTY(EditAnywhere, Category = "Crosshair")
    float CrosshairThickness = 1.5f;

    /** 中心点的半径（像素）；设 0 就不画点。 */
    UPROPERTY(EditAnywhere, Category = "Crosshair")
    float DotRadius = 1.5f;

    /** 进度弧的半径。 */
    UPROPERTY(EditAnywhere, Category = "Crosshair")
    float ArcRadius = 22.0f;

    /** 画不画"这一格挖了多少"的进度弧。 */
    UPROPERTY(EditAnywhere, Category = "Crosshair")
    bool bDrawProgressArc = true;

private:
    UAimAssistComponent* FindAimAssist() const;
    void DrawCrosshair(const FLinearColor& Color, bool bHasTarget);
    void DrawProgressArc(const FLinearColor& Color, float Progress);
};
