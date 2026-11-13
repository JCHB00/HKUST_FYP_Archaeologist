// 挖掘交互组件：挂在玩家角色身上，负责"从相机打一条球体扫描"，
// 打到 ADigSite 就调用它的 ApplyDig()。
//
// 为什么要单独做成组件，而不是直接写在 Character 的 C++ 里？
//  - 组件可以复用到别的 Pawn（比如以后做 NPC 考古学家）；
//  - 输入、射线、反馈这些东西和"角色本身"没有强耦合；
//  - 蓝图里 Add Component 就能用，不用改父类。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DigTypes.h"
#include "DigInteractionComponent.generated.h"

class ADigSite;

UCLASS(ClassGroup = (Archaeology), meta = (BlueprintSpawnableComponent))
class ARCHAEOLOGIST_API UDigInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDigInteractionComponent();

    /**
     * 从玩家相机往前扫一个球，尝试挖一块土。
     * 蓝图里只要有 InputAction（左键/扳机）就调它。
     */
    UFUNCTION(BlueprintCallable, Category = "Dig")
    EDigOutcome TryDig();

    /** 手动指定一条射线的起点和终点（做"从工具尖端往前挖"时用）。 */
    UFUNCTION(BlueprintCallable, Category = "Dig")
    EDigOutcome TryDigFromRay(FVector Start, FVector End);

    /** 上一次挖掘打到的 DigSite（没打到就是 null）。 */
    UFUNCTION(BlueprintPure, Category = "Dig")
    ADigSite* GetLastHitDigSite() const { return LastHitDigSite; }

    /** 每次挖掘结束都会触发，蓝图里接尘土/音效/提示。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dig|Events")
    void OnDigResult(EDigOutcome Outcome, const FVector& HitLocation, AActor* HitActor);

protected:
    /** 最远能挖多远（厘米）。300 = 3 米，大概是一个蹲着的人伸手的距离。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Trace", meta = (ClampMin = "10.0"))
    float DigDistance = 300.0f;

    /** 探测球半径（厘米）。给一点半径，玩家不用像素级瞄准。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Trace", meta = (ClampMin = "0.0"))
    float DigRadius = 6.0f;

    /** 用哪个碰撞通道找土。默认 Visibility；建议给土块单独建一个 Trace Channel。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Trace")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    /** 额外忽略的 Actor（比如手里拿着的工具，避免扫到自己手上）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Trace")
    TArray<TObjectPtr<AActor>> ExtraIgnoredActors;

    /** 调试用：把扫描线画出来。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Debug")
    bool bDrawDebug = false;

private:
    UPROPERTY(Transient)
    TObjectPtr<ADigSite> LastHitDigSite = nullptr;

    /** 取"玩家真正看的方向"：优先用相机，没有相机再退回 Pawn 眼睛。 */
    bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
};
