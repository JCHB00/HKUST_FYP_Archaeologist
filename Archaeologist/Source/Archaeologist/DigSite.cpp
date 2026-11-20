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
// 构造：创建组件、设置碰撞。
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
    // 目标：从 HISM 的实例下标反推出格子坐标。(在不查表下根据实例Instance的Index去计算坐标)
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
    if (!Blocks || InstanceIndex == INDEX_NONE)
    {
        return false;
    }
    FTransform  OutTransform;
    if(! Blocks->GetInstanceTransform(InstanceIndex, OutTransform, /*bWorldSpace=*/false))//坐标结果已经被写入OutTransform, 我们只需要创建一个FTransform 来承载即可
    {
        //if false
        return false;
    }
    OutCell = LocalToCell(OutTransform.GetLocation());//直接使用OutTransform
    return IsValidCell(OutCell);
    /*
    * 整体运行逻辑
    
    鼠标左键
      ↓
    射线打在 HISM 上
      ↓
    FHitResult
      ├─ Hit.Component = Blocks      ← 打中的是"土"吗
      ├─ Hit.Item      = 137         ← 打中的是"第几个实例"（只有 HISM 会填）
      └─ Hit.ImpactPoint = 世界坐标
      ↓
    CellFromInstanceIndex(137, Cell)  ← 现在写的这个function
      ↓
    Cell(5, 1, 2)
      ↓
    DigCell(Cell)                     ← 扣血 / 删除 / 出土
    */
}

// ===========================================================================
// 【核心 3】挖掘
// ===========================================================================
EDigOutcome ADigSite::ApplyDig(const FHitResult& Hit, AActor* Digger)
{
    // 目标：拿到一次碰撞命中，判断打到哪一格，然后交给 DigCell 去扣血/删除。
    //       "命中 -> 格子"的逻辑已经抽到 ResolveCellFromHit，准星那边用的是同一份。

    if (CellOccupied.Num() == 0)
    {
        return EDigOutcome::Invalid;
    }

    FDigCellCoord Cell;
    if (!ResolveCellFromHit(Hit, Cell))
    {
        OnDigRejected(Hit.ImpactPoint, EDigOutcome::NoBlockHit);
        return EDigOutcome::NoBlockHit;
    }

    return DigCell(Cell, Digger);
}

bool ADigSite::ResolveCellFromHit(const FHitResult& Hit, FDigCellCoord& OutCell) const
{
    if (CellOccupied.Num() == 0)
    {
        return false;
    }

    FIntVector Cell(0, 0, 0);
    bool bFoundCell = false;

    // 首选：HISM 会填 Hit.Item，直接反查实例下标（最准，不受格子边界影响）。
    if (Hit.Component.Get() == Blocks.Get() && Hit.Item != INDEX_NONE)
    {
        bFoundCell = CellFromInstanceIndex(Hit.Item, Cell);
    }

    // 退路：没有 Item（或命中来自别的网格）时，用 ImpactPoint 往土里缩一点再换算。
    //       为什么要缩：ImpactPoint 正好压在两个格子的公共面上时，FloorToInt 会偏到隔壁。
    if (!bFoundCell && Hit.bBlockingHit)
    {
        const double Inset = FMath::Max(1.0, (double)CellSize * 0.1);
        const FVector WorldPoint = Hit.ImpactPoint - Hit.ImpactNormal * Inset;
        const FVector LocalPoint = GetActorTransform().InverseTransformPosition(WorldPoint);
        const FIntVector Candidate = LocalToCell(LocalPoint);

        const int32 CandidateIndex = LinearIndex(Candidate);
        if (CellOccupied.IsValidIndex(CandidateIndex) && CellOccupied[CandidateIndex] != 0)
        {
            Cell = Candidate;
            bFoundCell = true;
        }
    }

    if (!bFoundCell)
    {
        return false;
    }

    OutCell = FDigCellCoord::FromIntVector(Cell);
    return true;
}

bool ADigSite::CanDigCell(FDigCellCoord Cell) const
{
    if (!IsCellOccupied(Cell))
    {
        return false;
    }

    if (bRequireExposed && !IsCellExposed(Cell))
    {
        return false;
    }

    return true;
}

float ADigSite::GetCellDigProgress(FDigCellCoord Cell) const
{
    const int32 Index = LinearIndex(Cell.ToIntVector());
    if (!CellDamage.IsValidIndex(Index) || !CellOccupied.IsValidIndex(Index))
    {
        return 0.f;
    }

    // 已经挖掉的格子算 100%。
    if (CellOccupied[Index] == 0)
    {
        return 1.f;
    }

    if (HitsPerBlock <= 0)
    {
        return 1.f;
    }

    return FMath::Clamp((float)CellDamage[Index] / (float)HitsPerBlock, 0.f, 1.f);
}
EDigOutcome ADigSite::DigCell(FDigCellCoord CellCoord, AActor* Digger)
{
    // 目标：真正处理"挖这一格"：扣血 -> 没血了删实例 -> 检查文物。
    //       所有挖掘入口（ApplyDig / 蓝图 / 作弊）最后都走这里，保证规则统一。

    const FIntVector Cell = CellCoord.ToIntVector();
    const int32 Index = LinearIndex(Cell);

    // 1) 非法格子，或者这一格已经没有土了
    if (!CellOccupied.IsValidIndex(Index) || CellOccupied[Index] == 0)
    {
        OnDigRejected(GetCellCenterWorld(CellCoord), EDigOutcome::NoBlockHit);
        return EDigOutcome::NoBlockHit;
    }

    // 2) 被土包住的格子挖不动（bRequireExposed 关掉就允许掏洞）
    if (bRequireExposed && !IsCellExposed(CellCoord))
    {
        OnDigRejected(GetCellCenterWorld(CellCoord), EDigOutcome::NotExposed);
        return EDigOutcome::NotExposed;
    }

    // 3) 后面播事件要用世界坐标，先算好
    const FVector WorldCenter = GetCellCenterWorld(CellCoord);

    // 4) 扣一次血
    ++CellDamage[Index];
    const int32 RemainingHits = FMath::Max(0, HitsPerBlock - CellDamage[Index]);

    // 5) 还没碎：只是掉了一小块
    if (RemainingHits > 0)
    {
        OnBlockDamaged(WorldCenter, CellCoord, RemainingHits);
        return EDigOutcome::Success;
    }

    // 6) 碎了：逻辑清空 -> 视觉移除 -> 播事件 -> 检查文物
    MarkCellEmpty(Cell);
    RemoveBlockInstance(Cell);
    OnBlockDug(WorldCenter, CellCoord);
    TryRevealArtifacts(Cell, Digger);

    return EDigOutcome::Success;
}

bool ADigSite::IsCellOccupied(FDigCellCoord Cell) const
{
    const int32 Index = LinearIndex(Cell.ToIntVector());
    return CellOccupied.IsValidIndex(Index) && CellOccupied[Index] != 0;
}

bool ADigSite::IsCellExposed(FDigCellCoord CellCoord) const
{
    // 目标：判断这一格是否"露在空气里"（能不能挖）。
    //   邻居越界      -> 这一面朝着网格外面的空气 -> 能挖
    //   邻居是空格子  -> 能挖
    //   六个邻居全有土 -> 被包死，挖不动

    const FIntVector Cell = CellCoord.ToIntVector();

    static const FIntVector Offsets[6] =
    {
        FIntVector( 1,  0,  0),
        FIntVector(-1,  0,  0),
        FIntVector( 0,  1,  0),
        FIntVector( 0, -1,  0),
        FIntVector( 0,  0,  1),
        FIntVector( 0,  0, -1),
    };

    for (const FIntVector& Offset : Offsets)
    {
        const FIntVector Neighbor = Cell + Offset;

        if (!IsValidCell(Neighbor))
        {
            return true;
        }

        const int32 NeighborIndex = LinearIndex(Neighbor);
        if (!CellOccupied.IsValidIndex(NeighborIndex) || CellOccupied[NeighborIndex] == 0)
        {
            return true;
        }
    }

    // 六个邻居全是土
    return false;
}

void ADigSite::MarkCellEmpty(const FIntVector& Cell)
{
    const int32 Index = LinearIndex(Cell);
    if (CellOccupied.IsValidIndex(Index))
    {
        CellOccupied[Index] = 0;
    }
}

int32 ADigSite::GetRemainingBlockCount() const
{
    // TODO: 遍历 CellOccupied，统计非 0 的个数。
    int32 count = 0;
    for (auto i : CellOccupied)
    {
        if (i) {
            count++;
        }
    }
    return count;
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
    // 目标：把这一格的 HISM 实例从画面上去掉（只管视觉，逻辑由 MarkCellEmpty 管）。
    //
    // 坑：Blocks->RemoveInstance(i) 之后，原来排在 i 后面的实例会整体往前挪一位
    //     （UE5.8 的 ISM 默认走"有序 RemoveAt"，不是 RemoveAtSwap，见
    //      Engine/Source/Runtime/Engine/Private/InstancedStaticMesh.cpp:3999），
    //     所以删完必须重建 CellToInstance，否则下一次挖掘会挖到隔壁。

    const int32 CellIndex = LinearIndex(Cell);
    if (!Blocks || !CellToInstance.IsValidIndex(CellIndex))
    {
        return;
    }

    const int32 InstanceIndex = CellToInstance[CellIndex];
    if (InstanceIndex != INDEX_NONE)
    {
        if (!Blocks->RemoveInstance(InstanceIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("DigSite: RemoveInstance(%d) 失败"), InstanceIndex);
        }
    }

    CellToInstance[CellIndex] = INDEX_NONE;

    // 索引整体前移，映射表已失效，必须重建。
    RebuildInstanceMapping();
}

void ADigSite::RebuildInstanceMapping()
{
    // 目标：以 HISM 里真实存在的实例为准，把 CellToInstance 整张表重算一遍。
    //       重建是 O(n)，n 最多几千，一次挖掘几微秒，永远正确；
    //       增量维护要依赖 RemoveInstance 是 shift 还是 swap，写错了就是隐蔽 bug。

    // 表长度跟逻辑格子数走，不是跟实例数走（土块越挖越少，格子永远是 384 个）。
    CellToInstance.Init(INDEX_NONE, CellOccupied.Num());

    if (!Blocks)
    {
        return;
    }

    const int32 NumInstances = Blocks->GetInstanceCount();
    for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
    {
        FTransform InstanceTransform;
        // bWorldSpace = false：取组件空间，和 AddInstance 时的坐标系一致。
        if (!Blocks->GetInstanceTransform(InstanceIndex, InstanceTransform, /*bWorldSpace=*/false))
        {
            continue;
        }

        const FIntVector Cell = LocalToCell(InstanceTransform.GetLocation());
        const int32 CellIndex = LinearIndex(Cell);
        if (CellToInstance.IsValidIndex(CellIndex))
        {
            CellToInstance[CellIndex] = InstanceIndex;
        }
    }
}

void ADigSite::TryRevealArtifacts(const FIntVector& Cell, AActor* Digger)
{
    // 目标：这一格土被挖掉的瞬间，检查有没有埋着文物，有就生成出来。

    if (BuriedArtifacts.Num() == 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (FDigBuriedArtifact& Entry : BuriedArtifacts) // 必须是引用，否则改不回数组
    {
        if (Entry.bRevealed || Entry.Cell.ToIntVector() != Cell)
        {
            continue;
        }

        // 先标记：即使生成失败（类为空、关卡还没加载完）也不该每次挖都重试。
        Entry.bRevealed = true;

        if (!Entry.ArtifactClass)
        {
            continue;
        }

        const FVector LocalPosition = GetCellCenterLocal(Cell) + Entry.Offset;
        const FVector WorldLocation = GetActorTransform().TransformPosition(LocalPosition);
        const FRotator WorldRotation = GetActorTransform().Rotator() + Entry.RotOffset;

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = Digger ? Digger : this;
        // 文物和土块在同一位置，不加 AlwaysSpawn 会因"生成位置被占用"静默失败。
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* Spawned = World->SpawnActor<AActor>(Entry.ArtifactClass, WorldLocation, WorldRotation, SpawnParams);
        Entry.SpawnedActor = Spawned;

        if (Spawned)
        {
            OnArtifactRevealed(Spawned, FDigCellCoord::FromIntVector(Cell));
        }
    }
}