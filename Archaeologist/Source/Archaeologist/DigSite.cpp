// 挖掘点的实现 —— 框架 + 实现指引。
//
// 约定：每个函数只给出"要做什么、按什么顺序做、用哪些引擎函数"，
//      具体代码由你自己写。写的时候建议按文件顺序从上往下写，
//      因为下面的函数依赖上面的（坐标换算 -> 网格重建 -> 挖掘 -> 实例维护）。

#include "DigSite.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

// ===========================================================================
// 构造：创建组件、设置碰撞。这段是纯样板，看懂就行，不用改。
// ===========================================================================
ADigSite::ADigSite()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    Blocks = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Blocks"));
    Blocks->SetupAttachment(SceneRoot);

    // 运行时要动态增删实例，组件必须是 Movable，否则引擎会报警告并且不动。
    Blocks->SetMobility(EComponentMobility::Movable);

    // 碰撞：土块要能被射线打到（Visibility 通道），也要能挡住玩家（WorldStatic）。
    Blocks->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Blocks->SetCollisionObjectType(ECC_WorldStatic);
    Blocks->SetCollisionResponseToAllChannels(ECR_Block);
    Blocks->SetGenerateOverlapEvents(false);

    // 兜底：没指定网格时用引擎自带的立方体，这样一拖进场景就有东西可挖。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMesh.Succeeded())
    {
        Blocks->SetStaticMesh(CubeMesh.Object);
    }
}

// ===========================================================================
// 编辑器里改参数时调用：改完立刻能看到土堆。
// ===========================================================================
void ADigSite::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // 不用做任何判断，直接调；RebuildSite 内部会处理"参数非法"的情况。
    RebuildSite();
}

void ADigSite::BeginPlay()
{
    Super::BeginPlay();
    // 兜底：运行时 SpawnActor 出来的实例，OnConstruction 已经跑过，一般没问题；
    // 但如果游戏里改了 GridSize / 网格，这里能救回来。
    if (Blocks && Blocks->GetInstanceCount() == 0)
    {
        RebuildSite();
    }
}

// ===========================================================================
// 【核心 1】重建整个土堆
// ===========================================================================
void ADigSite::RebuildSite()
{
    // 目标：清空并重新生成所有土块，把三张表初始化成"每格都满"，
    //       最后把文物重新埋回去。

    if (!Blocks)
    {
        return;
    }

    const UStaticMesh* Mesh = Blocks->GetStaticMesh();
    const FIntVector Size = GridSize.ToIntVector();
    const int32 TotalCells = FMath::Max(0, Size.X) * FMath::Max(0, Size.Y) * FMath::Max(0, Size.Z);

    // 参数非法（没网格 / 格子数为 0 / 格子尺寸太小）：
    // 清空实例和三张表，保持"要么都有、要么都没有"，不留中间态。
    if (!Mesh || TotalCells <= 0 || CellSize <= UE_KINDA_SMALL_NUMBER)
    {
        Blocks->ClearInstances();
        CellOccupied.Empty();
        CellDamage.Empty();
        CellToInstance.Empty();
        return;
    }

    // 重建前必须清空，否则每触发一次 OnConstruction，实例数就翻一倍。
    Blocks->ClearInstances();

    CellOccupied.Init(0, TotalCells);
    CellDamage.Init(0, TotalCells);
    CellToInstance.Init(INDEX_NONE, TotalCells);

    // 让任意尺寸的 StaticMesh 都正好填满一格：三个轴分别算缩放，并防除零。
    const FVector MeshSize = Mesh->GetBounds().BoxExtent * 2.0;
    BlockScale = FVector(
        CellSize / FMath::Max(MeshSize.X, (double)UE_KINDA_SMALL_NUMBER),
        CellSize / FMath::Max(MeshSize.Y, (double)UE_KINDA_SMALL_NUMBER),
        CellSize / FMath::Max(MeshSize.Z, (double)UE_KINDA_SMALL_NUMBER));

    UE_LOG(LogTemp, Warning, TEXT("RebuildSite: %d cells, scale=%s"),
        TotalCells, *BlockScale.ToString());

    for (int32 Z = 0; Z < Size.Z; ++Z)
    {
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                const FIntVector Cell(X, Y, Z);
                const int32 Index = LinearIndex(Cell);
                if (Index == INDEX_NONE)
                {
                    continue;
                }

                const FTransform BlockTransform(FRotator::ZeroRotator,
                    GetCellCenterLocal(Cell), BlockScale);

                CellOccupied[Index] = 1;
                CellDamage[Index] = 0;
                CellToInstance[Index] = Blocks->AddInstance(BlockTransform, /*bWorldSpace=*/false);
            }
        }
    }

    // 文物重新埋回去。
    for (FDigBuriedArtifact& Entry : BuriedArtifacts)
    {
        Entry.bRevealed = false;
        Entry.SpawnedActor = nullptr;
    }
}

// ===========================================================================
// 【核心 2】坐标换算 —— 这是整个系统的地基，先把它写对
// ===========================================================================

int32 ADigSite::LinearIndex(const FIntVector& Cell) const
{
    // 目标：把三维格子坐标压成一维数组下标。
    //
    // 数学：Index = X + Y*Size.X + Z*Size.X*Size.Y
    //
    // TODO:
    //   1) if (!IsValidCell(Cell)) return INDEX_NONE;
    //   2) 按上面的公式算出来返回（Size = GridSize.ToIntVector()）
    
    if (!IsValidCell(Cell))
    {
        return INDEX_NONE;
    }
    const FIntVector Size = GridSize.ToIntVector();
    const int32 Index = Cell.X + Cell.Y * Size.X + Cell.Z * Size.X * Size.Y;

    return  Index;
}

bool ADigSite::IsValidCell(const FIntVector& Cell) const
{
    // 目标：判断格子是否在 [0, Size) 范围内。
    // TODO: 三个轴都做 0 <= Cell.? && Cell.? < Size.? 的判断。
    const FIntVector Size = GridSize.ToIntVector();
    if (Cell.X < 0 || Cell.X >=  Size.X || Cell.Y < 0 || Cell.Y >= Size.Y || Cell.Z < 0 || Cell.Z >= Size.Z)
    {
        return false;
    }
    else
    {
        return true;
    }
    
}

FVector ADigSite::GetCellCenterLocal(const FIntVector& Cell) const
{
    // 目标：算出某个格子中心在 Actor 本地坐标系里的位置。
    //
    // 数学（想清楚这三个式子再动手）：
    //   HalfX =  Size.X * CellSize * 0.5     // XY 要以 Actor 原点为中心
    //   HalfY =  Size.Y * CellSize * 0.5
    //   Local.X = -HalfX + (Cell.X + 0.5) * CellSize
    //   Local.Y = -HalfY + (Cell.Y + 0.5) * CellSize
    //   Local.Z =  (Cell.Z + 0.5) * CellSize          // Z 从地面往上长，不需要居中
    //
    // 为什么要 +0.5：格子中心在格子起点往后半格。
    const FIntVector Size = GridSize.ToIntVector();
    
    const double HalfX = Size.X * CellSize * 0.5;    // XY 要以 Actor 原点为中心
    const double HalfY = Size.Y * CellSize * 0.5;

    FVector Local;
    Local.X = -HalfX + (Cell.X + 0.5) * CellSize;
    Local.Y = -HalfY + (Cell.Y + 0.5) * CellSize;
    Local.Z = (Cell.Z + 0.5) * CellSize;
    return Local;
}

FIntVector ADigSite::LocalToCell(const FVector& LocalPosition) const
{
    // 目标：GetCellCenterLocal 的逆运算：给一个本地坐标，算它落在哪个格子。
    //
    // 必须用 FMath::FloorToInt 而不是 RoundToInt —— 落在 [0, CellSize) 内都算第 0 格，
    // 用 RoundToInt 会让边界归属错半格。

    if (CellSize <= UE_KINDA_SMALL_NUMBER)
    {
        return FIntVector(-1, -1, -1);
    }

    const FIntVector Size = GridSize.ToIntVector();
    const double HalfX = Size.X * CellSize * 0.5;
    const double HalfY = Size.Y * CellSize * 0.5;

    const int32 CellX = FMath::FloorToInt((LocalPosition.X + HalfX) / CellSize);
    const int32 CellY = FMath::FloorToInt((LocalPosition.Y + HalfY) / CellSize);
    const int32 CellZ = FMath::FloorToInt(LocalPosition.Z / CellSize);

    return FIntVector(CellX, CellY, CellZ);
}

FVector ADigSite::GetCellCenterWorld(FDigCellCoord Cell) const
{
    // 目标：本地坐标 -> 世界坐标。
    // TODO: 用 GetActorTransform().TransformPosition(GetCellCenterLocal(Cell.ToIntVector()))
    return GetActorTransform().TransformPosition(GetCellCenterLocal(Cell.ToIntVector()));
}

bool ADigSite::CellFromInstanceIndex(int32 InstanceIndex, FIntVector& OutCell) const
{
    // 目标：从 HISM 的实例下标反推出格子坐标。
    //
    // 会用到的引擎函数：
    //   Blocks->GetInstanceTransform(InstanceIndex, OutTransform, /*bWorldSpace=*/false)
    //   返回 bool，取到的是"组件本地坐标"下的变换。
    //
    // TODO:
    //   1) if (!Blocks || InstanceIndex == INDEX_NONE) return false;
    //   2) 取实例变换，失败就 return false;
    //   3) OutCell = LocalToCell(变换的 GetLocation())
    //   4) return IsValidCell(OutCell)
    return false;
}

// ===========================================================================
// 【核心 3】挖掘
// ===========================================================================
EDigOutcome ADigSite::ApplyDig(const FHitResult& Hit, AActor* Digger)
{
    // 目标：拿到一次碰撞命中，判断打到哪一格，然后交给 DigCell 去扣血/删除。
    //       这个函数的重点是"命中 -> 格子"的容错。
    //
    // 会用到的引擎函数/成员：
    //   Hit.Component.Get()       被击中的组件（和 Blocks 比对，确认是土）
    //   Hit.Item                  实例化网格的"实例下标"（HISM 会填）
    //   Hit.bBlockingHit / Hit.ImpactPoint / Hit.ImpactNormal
    //   CellFromInstanceIndex()   反查格子
    //   FMath::Max(1.0f, CellSize * 0.1f)  算"往土里缩进去多少"
    //
    // 步骤：
    //   1) if (CellOccupied.Num() == 0) return EDigOutcome::Invalid;
    //   2) 声明 FIntVector Cell; bool bFoundCell = false;
    //   3) 首选路径：if (Hit.Component.Get() == Blocks.Get() && Hit.Item != INDEX_NONE)
    //         bFoundCell = CellFromInstanceIndex(Hit.Item, Cell);
    //   4) 退路（HISM 没填 Item、或者命中来自别的网格时）：
    //         if (!bFoundCell && Hit.bBlockingHit)
    //         把 ImpactPoint 转到本地坐标，再往法线反方向缩一点点（避免正好压在格子边界），
    //         然后 LocalToCell，再用 LinearIndex 确认这一格确实有土。
    //      （为什么要缩：ImpactPoint 正好落在两个格子的公共面上时，FloorToInt 会随机偏向一边）
    //   5) 都没找到：OnDigRejected(Hit.ImpactPoint, EDigOutcome::NoBlockHit)，返回 NoBlockHit
    //   6) return DigCell(FDigCellCoord::FromIntVector(Cell), Digger);
    return EDigOutcome::Invalid;
}

EDigOutcome ADigSite::DigCell(FDigCellCoord CellCoord, AActor* Digger)
{
    // 目标：真正处理"挖这一格"：扣血 -> 没血了删实例 -> 检查文物。
    //       所有挖掘入口（ApplyDig / 蓝图 / 作弊）最后都走这里，保证规则统一。
    //
    // 步骤：
    //   1) 取出 Index = LinearIndex(Cell)。非法、或者 CellOccupied[Index] == 0
    //      -> OnDigRejected(...NoBlockHit) 并返回 NoBlockHit。
    //   2) 悬空检查：if (bRequireExposed && !IsCellExposed(CellCoord))
    //      -> OnDigRejected(...NotExposed) 并返回 NotExposed。
    //   3) 先算 WorldCenter = GetCellCenterWorld(CellCoord)，后面播事件要用。
    //   4) ++CellDamage[Index]; RemainingHits = FMath::Max(0, HitsPerBlock - CellDamage[Index]);
    //   5) if (RemainingHits > 0) -> OnBlockDamaged(WorldCenter, CellCoord, RemainingHits);
    //      return Success;   // 还没碎，只是掉了一小块
    //   6) 碎了：
    //        MarkCellEmpty(Cell)        // 逻辑上标记为空
    //        RemoveBlockInstance(Cell)  // 视觉上移除实例（内部会重建映射）
    //        OnBlockDug(WorldCenter, CellCoord)
    //        TryRevealArtifacts(Cell, Digger)
    //        return Success;
    return EDigOutcome::Invalid;
}

bool ADigSite::IsCellOccupied(FDigCellCoord Cell) const
{
    // TODO: LinearIndex -> 检查 CellOccupied 里是不是 1。
    return false;
}

bool ADigSite::IsCellExposed(FDigCellCoord CellCoord) const
{
    // 目标：判断这一格是否"露在空气里"（能不能挖）。
    //
    // 思路：看六个邻居（±X / ±Y / ±Z）。
    //   - 邻居越界（不在网格里）-> 说明这一面朝着外面的空气 -> 可以挖
    //   - 邻居在网格里但 CellOccupied == 0 -> 也是空气 -> 可以挖
    //   - 六个邻居全是有土的格子 -> 被包死了 -> 挖不动
    //
    // 实现提示：
    //   把六个偏移写成一个 static const FIntVector Offsets[6] 数组循环，
    //   比写六段 if 好读。注意 C++ 里 FIntVector 支持 operator+。
    //
    // 还有一个特例：最上面一层（Cell.Z == Size.Z - 1）上方就是地表，
    // 严格说上面那格越界已经覆盖了，但如果你把 Z 循环写成从 0 开始，
    // 记得自己验证一下最上层能不能挖——这是最容易漏的一条。
    return false;
}

void ADigSite::MarkCellEmpty(const FIntVector& Cell)
{
    // TODO: LinearIndex -> CellOccupied[Index] = 0（记得先做 IsValidIndex 检查）。
}

int32 ADigSite::GetRemainingBlockCount() const
{
    // TODO: 遍历 CellOccupied，统计非 0 的个数。
    return 0;
}

void ADigSite::ResetSite()
{
    // TODO: 直接调用 RebuildSite()。
    RebuildSite();
}

// ===========================================================================
// 【核心 4】HISM 实例维护 —— 这里有一个必须知道的坑
// ===========================================================================
void ADigSite::RemoveBlockInstance(const FIntVector& Cell)
{
    // 目标：从 HISM 里删掉这一格的实例。
    //
    // 必须知道的坑：
    //   Blocks->RemoveInstance(i) 之后，原来排在 i 后面的实例会整体往前挪一位
    //   （UE5.8 的 ISM 默认走"有序 RemoveAt"，不是 RemoveAtSwap，
    //    见 Engine/Source/Runtime/Engine/Private/InstancedStaticMesh.cpp:3999）。
    //   所以你不能只把 CellToInstance[这一格] 设成 -1 —— 那样后面所有格子的映射
    //   都会错位一格，下一次挖掘就会挖到隔壁，甚至删错东西。
    //
    // 步骤：
    //   1) Index = LinearIndex(Cell)；CellToInstance 里取 InstanceIndex。
    //   2) if (Blocks && InstanceIndex != INDEX_NONE) Blocks->RemoveInstance(InstanceIndex);
    //   3) CellToInstance[Index] = INDEX_NONE;
    //   4) RebuildInstanceMapping();   // 交给下面那个函数兜底
}

void ADigSite::RebuildInstanceMapping()
{
    // 目标：把 CellToInstance 整张表按"当前 HISM 里真实的实例"重建。
    //
    // 为什么重建而不是增量维护：
    //   增量维护要知道 RemoveInstance 到底是 swap 还是 shift（依赖版本和 cvar），
    //   写错了就是隐蔽 bug；重建是 O(n)，n 最多几千，一次挖掘几微秒，永远正确。
    //
    // 步骤：
    //   1) CellToInstance.Init(INDEX_NONE, CellOccupied.Num());
    //   2) for 每个实例 i in [0, Blocks->GetInstanceCount())：
    //        Blocks->GetInstanceTransform(i, T, false) 取本地变换
    //        Cell = LocalToCell(T.GetLocation())
    //        CellIndex = LinearIndex(Cell)，有效就 CellToInstance[CellIndex] = i
}

// ===========================================================================
// 【核心 5】文物出土
// ===========================================================================
void ADigSite::TryRevealArtifacts(const FIntVector& Cell, AActor* Digger)
{
    // 目标：这一格被挖掉的瞬间，检查有没有埋着文物，有就生成出来。
    //
    // 会用到的引擎函数：
    //   GetWorld()->SpawnActor<AActor>(Class, Location, Rotation, SpawnParams)
    //   GetActorTransform().TransformPosition(...) / .Rotator()
    //
    // 步骤：
    //   1) 遍历 BuriedArtifacts（用 for (FDigBuriedArtifact& Entry : BuriedArtifacts)，
    //      注意必须是"引用"，否则改了不会写回数组）。
    //   2) 跳过 bRevealed == true 的、跳过 Entry.Cell 和当前 Cell 不相等的。
    //   3) Entry.bRevealed = true;（先标记，避免重复生成）
    //   4) if (!Entry.ArtifactClass) continue;
    //   5) 算生成位置：格子中心 + Entry.Offset，再转到世界坐标；
    //      朝向 = Actor 朝向 + Entry.RotOffset。
    //   6) FActorSpawnParameters：Owner 设成 Digger（没有就给 this），
    //      SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    //      （不然文物和土块重叠时会生成失败）。
    //   7) Entry.SpawnedActor = Spawned;
    //      生成成功就 OnArtifactRevealed(Spawned, FDigCellCoord::FromIntVector(Cell));
}
