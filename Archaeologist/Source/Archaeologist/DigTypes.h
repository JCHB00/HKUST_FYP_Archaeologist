// 挖掘系统共用类型定义
// 这个文件只放"数据结构"和"枚举"，不放任何逻辑，方便其他类引用。

#pragma once

#include "CoreMinimal.h"
#include "DigTypes.generated.h"

/**
 * 一次挖掘尝试的结果。
 * 蓝图里可以拿它来决定播什么反馈：成功了播尘土，失败播"挖不动"的提示音。
 */
UENUM(BlueprintType)
enum class EDigOutcome : uint8
{
    Success      UMETA(DisplayName = "成功"),
    NoBlockHit   UMETA(DisplayName = "没打到东西"),
    NotExposed   UMETA(DisplayName = "被土包着，挖不动"),
    WrongTarget  UMETA(DisplayName = "打到的不是土"),
    Invalid      UMETA(DisplayName = "参数错误")
};

/**
 * 格子坐标（整数）。
 *
 * 为什么不用引擎自带的 FIntVector？
 * 因为 FIntVector 在 UE5 里是 UE::Math::TIntVector3<int32>，**不是 USTRUCT**，
 * 不能出现在 UPROPERTY / UFUNCTION 里（编辑器面板会报 "not a USTRUCT"）。
 * 自己包一层最省事，还能顺便加 ToIntVector() 做转换。
 */
USTRUCT(BlueprintType)
struct FDigCellCoord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig")
    int32 X = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig")
    int32 Y = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig")
    int32 Z = 0;

    FDigCellCoord() = default;

    FDigCellCoord(int32 InX, int32 InY, int32 InZ)
        : X(InX), Y(InY), Z(InZ)
    {
    }

    /** 转成引擎内部用的整数向量。 */
    FIntVector ToIntVector() const
    {
        return FIntVector(X, Y, Z);
    }

    static FDigCellCoord FromIntVector(const FIntVector& In)
    {
        return FDigCellCoord(In.X, In.Y, In.Z);
    }

    bool operator==(const FDigCellCoord& Other) const
    {
        return X == Other.X && Y == Other.Y && Z == Other.Z;
    }
};

/**
 * 一个"埋在土里的文物"。
 * 在 DigSite 的细节面板里填：格子坐标 + 要生成的蓝图类。
 * 当这个格子被挖掉时，DigSite 会自动 Spawn 它，并触发 OnArtifactRevealed 事件。
 */
USTRUCT(BlueprintType)
struct FDigBuriedArtifact
{
    GENERATED_BODY()

    /** 埋在哪个格子里（格子坐标，不是世界坐标）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    FDigCellCoord Cell;

    /** 挖开这一格时生成的 Actor 蓝图。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    TSubclassOf<AActor> ArtifactClass;

    /** 相对格子中心的偏移（厘米），用来微调出土位置。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    FVector Offset = FVector::ZeroVector;

    /** 出土朝向。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    FRotator RotOffset = FRotator::ZeroRotator;

    /** 运行时生成出来的 Actor（不用手填）。 */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Artifact")
    TObjectPtr<AActor> SpawnedActor = nullptr;

    /** 是否已经出土（不用手填）。 */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Artifact")
    bool bRevealed = false;
};
