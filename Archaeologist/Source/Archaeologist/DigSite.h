// 挖掘点（Dig Site）：把一块土做成"可逐块挖掉"的体素格。
//
// 设计要点：
//  1. 整块土的所有"土块"用 **一个** HierarchicalInstancedStaticMeshComponent（HISM）渲染，
//     1280 个格子也只有 1 个 draw call，比摆 1280 个 StaticMeshActor 快几个数量级。
//  2. 逻辑上是一个三维布尔数组（CellOccupied），HISM 只负责"看得见"。
//  3. 每块土有血量（HitsPerBlock），被打够次数才碎，这样才有"一点点抠"的手感。
//  4. 只有"露在外面"的土块能挖（六邻居有一个是空气），否则会挖出悬空的洞。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"
#include "DigTypes.h"
#include "DigSite.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS(Blueprintable)
class ARCHAEOLOGIST_API ADigSite : public AActor
{
    GENERATED_BODY()

public:
    ADigSite();

    // ---------------------------------------------------------------------
    // 生命周期
    // ---------------------------------------------------------------------

    /** 在编辑器里拖参数、或用 SpawnActor 生成时都会调用，用来重建格子。 */
    virtual void OnConstruction(const FTransform& Transform) override;

    virtual void BeginPlay() override;

    // ---------------------------------------------------------------------
    // 供蓝图调用的主接口
    // ---------------------------------------------------------------------

    /**
     * 核心函数：拿一个射线/扫描的命中结果，尝试打掉一块土。
     * 由 UDigInteractionComponent 调用，也可以在蓝图里手动调用（比如做"铲子一下挖一片"）。
     */
    UFUNCTION(BlueprintCallable, Category = "Dig Site")
    EDigOutcome ApplyDig(const FHitResult& Hit, AActor* Digger);

    /** 直接按格子挖（作弊命令 / 剧情脚本 / 一键清场用）。 */
    UFUNCTION(BlueprintCallable, Category = "Dig Site")
    EDigOutcome DigCell(FDigCellCoord Cell, AActor* Digger);

    /** 这一格还有土吗？ */
    UFUNCTION(BlueprintPure, Category = "Dig Site")
    bool IsCellOccupied(FDigCellCoord Cell) const;

    /** 这一格是否露在外面（能挖）？ */
    UFUNCTION(BlueprintPure, Category = "Dig Site")
    bool IsCellExposed(FDigCellCoord Cell) const;

    /** 格子中心的世界坐标（生成尘土特效、放音效都用它）。 */
    UFUNCTION(BlueprintPure, Category = "Dig Site")
    FVector GetCellCenterWorld(FDigCellCoord Cell) const;

    /** 还剩多少块土（做"完成度"进度条用）。 */
    UFUNCTION(BlueprintPure, Category = "Dig Site")
    int32 GetRemainingBlockCount() const;

    /** 整个挖掘点恢复原样（重开一局 / 撤销）。 */
    UFUNCTION(BlueprintCallable, Category = "Dig Site")
    void ResetSite();

    // ---------------------------------------------------------------------
    // 给蓝图实现的事件（C++ 里只声明，逻辑写在蓝图里）
    // ---------------------------------------------------------------------

    /** 打中但还没碎：播尘土、音效、铲子动画。RemainingHits = 还要打几下。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dig Site|Events")
    void OnBlockDamaged(const FVector& WorldLocation, FDigCellCoord Cell, int32 RemainingHits);

    /** 一块土碎掉了。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dig Site|Events")
    void OnBlockDug(const FVector& WorldLocation, FDigCellCoord Cell);

    /** 文物出土了。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dig Site|Events")
    void OnArtifactRevealed(AActor* Artifact, FDigCellCoord Cell);

    /** 挖失败（被打回"挖不动"）。 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dig Site|Events")
    void OnDigRejected(const FVector& WorldLocation, EDigOutcome Reason);

protected:
    // ---------------------------------------------------------------------
    // 可在细节面板调整的参数
    // ---------------------------------------------------------------------

    /** 以"格子"为单位的尺寸：X/Y 是平面，Z 是层数（深度）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dig Site|Grid")
    FDigCellCoord GridSize = FDigCellCoord(8, 8, 6);

    /** 每一格多大（厘米）。25 = 25cm 的土块，比较接近真实洛阳铲/手铲的挖掘粒度。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dig Site|Grid", meta = (ClampMin = "1.0"))
    float CellSize = 25.0f;

    /** 每块土要打几下才碎。1 = 一下就碎（像现在的版本），3 = 有"抠"的手感。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dig Site|Gameplay", meta = (ClampMin = "1"))
    int32 HitsPerBlock = 3;

    /** 是否要求土块"露在外面"才能挖。关掉就能从内部掏洞（调试用）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dig Site|Gameplay")
    bool bRequireExposed = true;

    /** 埋在土里的文物列表。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dig Site|Artifacts")
    TArray<FDigBuriedArtifact> BuriedArtifacts;

    /** 渲染土块的组件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dig Site|Components")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Blocks;

    // ---------------------------------------------------------------------
    // 内部状态
    // ---------------------------------------------------------------------

    /** 每个格子是否还有土（1 = 有，0 = 空）。扁平数组，长度 = X*Y*Z。 */
    UPROPERTY(Transient)
    TArray<uint8> CellOccupied;

    /** 每个格子已经被打了几下。 */
    UPROPERTY(Transient)
    TArray<int32> CellDamage;

    /** 每个格子对应 HISM 里的第几个实例（-1 = 没有）。 */
    UPROPERTY(Transient)
    TArray<int32> CellToInstance;

    /** 渲染用缩放（让任意尺寸的 StaticMesh 都正好填满一格）。 */
    FVector BlockScale = FVector::OneVector;

    // ---------------------------------------------------------------------
    // 内部函数
    // ---------------------------------------------------------------------

    /** 清空并重建整个网格（编辑器里改完参数立刻能看到）。 */
    void RebuildSite();

    /** 格子的线性下标；非法格子返回 INDEX_NONE。 */
    int32 LinearIndex(const FIntVector& Cell) const;

    bool IsValidCell(const FIntVector& Cell) const;

    /** 格子中心（Actor 本地坐标）。 */
    FVector GetCellCenterLocal(const FIntVector& Cell) const;

    /** Actor 本地坐标 -> 格子坐标（可能越界，调用方自己用 IsValidCell 检查）。 */
    FIntVector LocalToCell(const FVector& LocalPosition) const;

    /** 从 HISM 实例下标反查格子。 */
    bool CellFromInstanceIndex(int32 InstanceIndex, FIntVector& OutCell) const;

    /** 移除一块土（会重排 HISM 的实例下标，所以之后要重建映射）。 */
    void RemoveBlockInstance(const FIntVector& Cell);

    /** 重建 CellToInstance：直接读回每个实例的变换，版本无关，永远正确。 */
    void RebuildInstanceMapping();

    /** 检查这一格里有没有文物，有就生成出来（Digger 会作为生成出来的 Actor 的 Owner）。 */
    void TryRevealArtifacts(const FIntVector& Cell, AActor* Digger = nullptr);

    /** 把一个格子的土标记为挖掉（不含 HISM 操作）。 */
    void MarkCellEmpty(const FIntVector& Cell);
};
