// 瞄准辅助组件：每隔一小段时间扫一次"玩家现在瞄着什么"，
// 把结果画成世界里的目标格子高亮，并广播状态给准星（HUD）。
//
// 为什么单独一个组件：
//  - 它只读不写，不影响挖掘本身，删掉它游戏照样能玩；
//  - 准星（HUD）只从这里取状态，不自己打射线，避免"看到绿、按下去打空"。
//
// 注意：这里没有任何"从身体射出的光束"。光束的起点只能取相机或手部插槽，
//       两者都会横穿画面挡住视线，所以只保留"贴在土面上的格子方框"这一种表现。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DigTypes.h"
#include "AimAssistComponent.generated.h"

class ADigSite;
class UDigInteractionComponent;
class ULineBatchComponent;

/** 瞄准状态变化时广播（准星、音效、角色预备动作都可以接这个）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnAimChanged,
    EDigAimState, State,
    FVector, HitLocation,
    bool, bHasTarget,
    float, Progress);

UCLASS(ClassGroup = (Archaeology), meta = (BlueprintSpawnableComponent))
class ARCHAEOLOGIST_API UAimAssistComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAimAssistComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---- 给 HUD 读 ----

    UFUNCTION(BlueprintPure, Category = "Aim")
    EDigAimState GetAimState() const { return AimState; }

    /** 当前目标格已经挖了多少（0~1），HUD 拿它画进度弧。 */
    UFUNCTION(BlueprintPure, Category = "Aim")
    float GetAimProgress() const { return AimProgress; }

    /** 瞄着"土"（可挖或挖不动都算），用来决定准星收不收拢。 */
    UFUNCTION(BlueprintPure, Category = "Aim")
    bool HasAimTarget() const;

    /** 当前状态对应的颜色，准星和世界高亮用同一个。 */
    UFUNCTION(BlueprintPure, Category = "Aim")
    FLinearColor GetAimColor() const;

    UPROPERTY(BlueprintAssignable, Category = "Aim")
    FOnAimChanged OnAimChanged;

protected:
    // ---- 更新频率 ----

    /** 每次扫描的间隔（秒）。0.05 = 20Hz，准星已经很跟手，开销可以忽略。 */
    UPROPERTY(EditAnywhere, Category = "Aim|Update", meta = (ClampMin = "0.01"))
    float AimUpdateInterval = 0.05f;

    // ---- 视觉 ----

    /** 在命中的那一格上画一个贴着土面的方框。 */
    UPROPERTY(EditAnywhere, Category = "Aim|Visual")
    bool bDrawCellHighlight = true;

    /** 方框线宽。 */
    UPROPERTY(EditAnywhere, Category = "Aim|Visual", meta = (ClampMin = "0.1"))
    float HighlightThickness = 2.0f;

    /** 命中点上的小圆环半径；设 0 就不画。 */
    UPROPERTY(EditAnywhere, Category = "Aim|Visual")
    float ImpactRingRadius = 4.0f;

    // ---- 颜色 ----

    UPROPERTY(EditAnywhere, Category = "Aim|Colors")
    FLinearColor DiggableColor = FLinearColor(0.15f, 1.0f, 0.35f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Aim|Colors")
    FLinearColor BlockedColor = FLinearColor(1.0f, 0.65f, 0.1f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Aim|Colors")
    FLinearColor WrongTargetColor = FLinearColor(1.0f, 0.2f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Aim|Colors")
    FLinearColor NoTargetColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.55f);

private:
    void UpdateAim();
    void EnsureLineBatch();
    void DrawVisuals(const FHitResult& Hit, bool bHit, ADigSite* Site, const FDigCellCoord& Cell, bool bHasCell);
    void DrawCellFace(const FVector& CellCenter, const FVector& ImpactPoint, const FVector& FaceNormal, float HalfSize, const FLinearColor& Color);
    void DrawImpactRing(const FVector& Center, const FVector& Normal, const FLinearColor& Color);

    /** 线条批处理组件：不用任何美术资源就能画出线框。 */
    UPROPERTY(Transient)
    TObjectPtr<ULineBatchComponent> AimLines;

    UPROPERTY(Transient)
    TObjectPtr<UDigInteractionComponent> DigInteraction;

    EDigAimState AimState = EDigAimState::NoTarget;
    float AimProgress = 0.0f;
    FVector AimHitLocation = FVector::ZeroVector;
    float TimeAccumulator = 0.0f;
};
