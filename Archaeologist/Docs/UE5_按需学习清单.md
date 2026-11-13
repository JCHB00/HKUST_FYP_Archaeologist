# UE5 C++ 按需学习清单（Archaeologist 项目）

> 前提：你已经有 C++ 编程经验。所以这份清单**不讲语法**，只讲"UE 和普通 C++ 项目不一样的地方"。
> 用法：写完一个函数就回来查一次，不要从头读到尾。
> 环境：UE 5.8 / Visual Studio 2026 / 模块名 `Archaeologist`

---

## 0. 先记住一句话

> **UE 的 C++ 代码不是"程序"，是"给蓝图和编辑器看的数据说明书"。**

你写的每个 `UPROPERTY`、每个 `UFUNCTION`，本质上都是在向三样东西登记：
1. **蓝图**（能不能在 BP 里读写/调用）
2. **编辑器**（能不能在细节面板调、能不能序列化进 .uasset）
3. **GC**（这个对象还活着的时候，别回收它指向的东西）

反过来，没登记的东西统统不管：不显示、不存盘、可能被回收。**新手 90% 的"玄学 bug"都在这里。**

---

## 1. 编译系统：为什么改一行要等一分钟（半天）

| 概念 | 是什么 | 在你项目里 |
|---|---|---|
| `.uproject` | 项目描述文件（引擎版本、模块列表、插件开关） | `Archaeologist.uproject` |
| `*.Build.cs` | **模块**的依赖清单（相当于 CMake 的 target_link_libraries） | `Source/Archaeologist/Archaeologist.Build.cs`，现在只依赖 `Core/CoreUObject/Engine/InputCore` |
| `*.Target.cs` | 编译**目标**（Editor / Game / Shipping 各一份） | `Source/Archaeologist.Target.cs`、`ArchaeologistEditor.Target.cs` |
| 模块（Module） | 一个 DLL。项目可以拆成多个模块 | 目前只有一个 `Archaeologist` |
| `ARCHAEOLOGIST_API` | 导出宏，让别的模块能链接到这个类 | `class ARCHAEOLOGIST_API ADigSite : public AActor` |
| UHT | Unreal Header Tool，**在编译前**扫你的头文件，生成 `.generated.h` | `DigSite.generated.h` |

**几个必须知道的规矩：**

1. `#include "XXX.generated.h"` **必须是这个头文件的最后一个 include**，否则 UHT 报错。
2. 头文件名必须和类名同名（`DigSite.h` ↔ `ADigSite`），否则 UHT 找不到。
3. `GENERATED_BODY()` 宏必须写在类体第一行，它会把 UHT 生成的一堆东西展开进来。
4. 新增 `.cpp` 后，UBT 会自动发现（它扫目录），但 **VS 的解决方案要重新生成**：右键 `.uproject` → *Generate Visual Studio project files*。
5. **想改类结构（加 `UCLASS`/`UPROPERTY`/`UFUNCTION`、加成员变量、改继承）→ 必须先关掉编辑器，全量编译。**
   Live Coding（`Ctrl+Alt+F11`）只能热补丁**函数体**，改不了类的内存布局。你已经踩过一次：
   ```
   Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11
   ```
6. 命令行全量编译（编辑器关掉后才能用）：
   ```
   D:\Tools\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat ArchaeologistEditor Win64 Development ^
     -Project="D:\HKUST\...\Archaeologist.uproject" -WaitMutex -NoHotReload
   ```
7. **编辑器模块**（`Type = "Editor"`）只能被编辑器加载，不能打包进游戏。我的 `Plugins/ArchaeoDump` 就是这种——用完可以整个删掉，不影响游戏。

> **学习动作**：打开 `Intermediate/Build/Win64/UnrealEditor/Inc/Archaeologist/UHT/DigSite.generated.h`（编译过一次就有），看看 `UPROPERTY` 到底生成了什么。这是理解反射最快的路，5 分钟胜过读十篇文章。

---

## 2. 反射三件套：UCLASS / UPROPERTY / UFUNCTION（1 天，最重要）

### 2.1 四个宏的分工

| 宏 | 位置 | 作用 | 你项目里的例子 |
|---|---|---|---|
| `UCLASS()` | 类上方 | 注册这个类，让它能被 `NewObject`/`SpawnActor`、能被蓝图继承 | `UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))` |
| `GENERATED_BODY()` | 类体第一行 | 展开 UHT 生成的内容 | 每个 `UCLASS` 都有 |
| `UPROPERTY()` | 成员变量上方 | 暴露给 GC / 编辑器 / 序列化 / 蓝图 | `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dig Site\|Grid")` |
| `UFUNCTION()` | 函数上方 | 暴露给蓝图 / 网络 / 委托 | `UFUNCTION(BlueprintCallable, Category="Dig Site")` |

### 2.2 `UPROPERTY` 最常用的说明符

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig|Trace", meta = (ClampMin = "10.0"))
float DigDistance = 300.0f;
```

| 说明符 | 含义 | 少了它会怎样 |
|---|---|---|
| `EditAnywhere` | 细节面板可改（实例和 CDO 都能改） | 面板里看不到这个参数 |
| `VisibleAnywhere` | 面板里只读（一般是组件引用） | 同上 |
| `BlueprintReadOnly` / `BlueprintReadWrite` | 蓝图里能不能读写 | 蓝图拿不到，得加 Getter |
| `Transient` | 不存盘（运行时状态） | 每次保存 .uasset 都把它写进去，越存越大 |
| `meta=(ClampMin/Max)` | 面板上限制数值范围 | 能填出 -99999 这种值 |
| **什么都不写** | **不参与 GC、不序列化、不暴露** | **裸指针会被 GC 悄悄清空 → 随机崩溃** |

**最关键的一条**：`TArray<AToolBase*> ToolActors;` 如果不写 `UPROPERTY()`，GC 不知道你还在用它，会把它指向的 Actor 回收掉，然后你的指针就是野指针。`ToolInventoryComponent.h:51` 写了 `UPROPERTY()`，这是对的。

### 2.3 `UFUNCTION` 的四种用法（别搞混）

| 写法 | 谁调用谁 | 用在哪 |
|---|---|---|
| `BlueprintCallable` | 蓝图调 C++ | 蓝图的"执行"引脚能连进来 |
| `BlueprintPure` | 蓝图调 C++，无执行引脚 | Getter、数学计算 |
| `BlueprintImplementableEvent` | **C++ 调蓝图**，C++ 里没有实现 | `OnBlockDug`、`OnArtifactRevealed`——C++ 只负责"喊一声"，表现全在 BP |
| `BlueprintNativeEvent` | 两边都能实现，C++ 写 `_Implementation` | 需要默认实现又允许蓝图覆盖时 |

**这是 UE 最重要的设计模式**：C++ 管逻辑和规则，蓝图管表现和参数。`ADigSite` 里那四个 `BlueprintImplementableEvent` 就是给蓝图留的插槽——你在 BP 里接上就能播尘土、出声、弹提示，不用改一行 C++。

### 2.4 命名前缀（强制规范，别自创）

| 前缀 | 类型 | 例子 |
|---|---|---|
| `U` | UObject 派生（非 Actor） | `UDigInteractionComponent` |
| `A` | Actor 派生 | `ADigSite`、`AToolBase` |
| `F` | 普通结构体 / 非 UObject 类 | `FDigCellCoord`、`FHitResult` |
| `E` | 枚举 | `EDigOutcome` |
| `T` | 模板 | `TArray`、`TObjectPtr` |
| `I` | 接口 | `IBPI_Interact` |
| `b` | bool 成员 | `bRequireExposed`、`bDrawDebug` |

---

## 3. 内存与生命周期（半天）

### 3.1 两种"创建"

```cpp
// UObject（不是 Actor）：组件、资产、数据对象
UDigInteractionComponent* Comp = NewObject<UDigInteractionComponent>(this);

// Actor：能放进世界的
ADigSite* Site = GetWorld()->SpawnActor<ADigSite>(SiteClass, Location, Rotation, Params);
```

**Actor 永远不 `NewObject`**，因为它需要注册到 World、需要 BeginPlay。

### 3.2 生命周期顺序（很重要的表）

| 时机 | 什么时候跑 | 能做什么 / 不能做什么 |
|---|---|---|
| 构造函数 | 类加载、CDO 创建时**一次** | 只能用 `CreateDefaultSubobject` 建组件、设默认值。**`GetWorld()` 是 nullptr**，别在这查世界 |
| `OnConstruction` | 编辑器里拖动/改参数时；`SpawnActor` 后 | 按参数生成东西（`RebuildSite` 就挂在这） |
| `PostInitializeComponents` | 组件都建好之后 | 组件间互相找 |
| `BeginPlay` | 世界开始游戏时 | 正常逻辑入口。**必须调 `Super::BeginPlay()`** |
| `Tick` | 每帧 | 别忘了 `PrimaryActorTick.bCanEverTick = true` |
| `EndPlay` | 销毁/关卡切换 | 清理定时器、解绑委托 |

### 3.3 指针怎么写

| 写法 | 用途 |
|---|---|
| `TObjectPtr<UStaticMeshComponent>` | UE5 推荐的**成员变量**写法（`UPROPERTY` 里），本质是带 GC 追踪的指针 |
| `AToolBase*` | 局部变量、函数参数照常写裸指针 |
| `TWeakObjectPtr<T>` | 不想持有强引用（比如"最后一个被打到的土堆"） |
| `IsValid(Ptr)` | **比 `Ptr != nullptr` 安全**：对象已经被标记销毁但内存还没释放时，`!= nullptr` 是 true，`IsValid` 是 false |

---

## 4. 容器 / 字符串 / 数学：和 STL 的差别（2 小时）

| UE | 对应 STL | 必须记住的差异 |
|---|---|---|
| `TArray<T>` | `std::vector` | `.Num()` 不是 `.size()`；`.Add()` 不是 `.push_back()`；`IsValidIndex(i)` 比自己判断好 |
| `TMap<K,V>` | `std::unordered_map` | 遍历顺序不保证；`.Contains()` |
| `TSet<T>` | `std::unordered_set` | — |
| `FString` | `std::string` | 必须用 `TEXT("...")` 包字符串；`*Str` 取 `const TCHAR*` |
| `FName` | 常量字符串/ID | 大小写不敏感、全局唯一、比较极快。**用于名字、ID、Socket 名** |
| `FText` | 本地化文本 | 给玩家看的文字一律用 `FText`（`FToolData::ToolName` 就是） |

**数学库**：

| 类型 | 说明 |
|---|---|
| `FVector` | **UE5 里是 double（LWC 大世界坐标）**，不是 float。老教程写 `float` 会隐式转换，性能敏感处要用 `FVector3f` |
| `FIntVector` | 整数三维坐标。**不是 USTRUCT，不能进 `UPROPERTY`**（所以我们才写了 `FDigCellCoord`） |
| `FRotator` | 欧拉角（Pitch/Yaw/Roll，单位是度） |
| `FQuat` | 四元数，插值用它 |
| `FTransform` | 位置+旋转+缩放，`FTransform(Rotator, Location, Scale)` |
| `FMath::FloorToInt` / `Max` / `Clamp` | 用它，别用 `std::` |

> 小抄：`FRotator::Vector()` = 这个朝向的正前方单位向量。我们的射线方向就是 `ViewRotation.Vector() * DigDistance`。

---

## 5. 这两个文件 ↔ 你要查的 API

### 5.1 DigSite.cpp（写之前只查这些）

| 你要写的函数 | 查什么 | 文档关键词 |
|---|---|---|
| `LinearIndex` / `IsValidCell` | 纯 C++ 数学 | — |
| `GetCellCenterLocal` / `LocalToCell` | `FMath::FloorToInt` | `FMath::FloorToInt` |
| `RebuildSite` | `TArray::Init`、`UStaticMesh::GetBounds` | `TArray::Init`、`FBoxSphereBounds` |
| `RebuildSite` | `HISM::AddInstance` | `UHierarchicalInstancedStaticMeshComponent::AddInstance` |
| `CellFromInstanceIndex` | `HISM::GetInstanceTransform` | 同上，注意 `bWorldSpace` 参数 |
| `ApplyDig` | `FHitResult` 的字段 | `FHitResult::Item`、`FHitResult::Component`、`FHitResult::ImpactPoint` |
| `DigCell` | 无新 API | 复用上面的 |
| `RemoveBlockInstance` | `HISM::RemoveInstance` | 注意**索引前移**的坑（见下面第 7 节） |
| `TryRevealArtifacts` | `SpawnActor` 的参数 | `FActorSpawnParameters`、`ESpawnActorCollisionHandlingMethod` |

### 5.2 DigInteractionComponent.cpp（3 个函数，3 个 API）

| 函数 | 查什么 |
|---|---|
| `GetViewPoint` | `APlayerCameraManager::GetCameraLocation` / `GetCameraRotation`、`AActor::GetActorEyesViewPoint` |
| `TryDigFromRay` | `UWorld::SweepSingleByChannel`、`FCollisionShape::MakeSphere`、`FCollisionQueryParams` |
| `TryDig` | 无新 API，纯拼接 |

> 查文档的正确姿势：官方 API Reference 搜类名 → 看 `Syntax` 那一栏的完整签名（参数类型、默认值），**不要看中文博客**，那些大多是 4.x 时代的。

---

## 6. 调试：比学设计模式更急（1 小时）

| 手段 | 怎么用 | 什么时候用 |
|---|---|---|
| `UE_LOG` | `UE_LOG(LogTemp, Warning, TEXT("Cell=%s"), *Cell.ToString())` | 最常用。输出到编辑器的 **Output Log** 和 VS 的 Output 窗口 |
| 自定义日志分类 | `DECLARE_LOG_CATEGORY_EXTERN(LogDig, Log, All);` + `DEFINE_LOG_CATEGORY(LogDig);` | 日志太多时按分类过滤 |
| VS 断点 | 编辑器开着，VS 里 *调试 → 附加到进程 → UnrealEditor.exe* | 逻辑复杂、要看变量时 |
| `DrawDebugLine` / `DrawDebugSphere` | `#include "DrawDebugHelpers.h"` | 验证射线到底打在哪（我们组件的 `bDrawDebug` 就是干这个） |
| `check()` / `ensure()` | 表达式为假时崩 / 只报错不崩 | 抓"不该发生"的情况 |
| `IsValid()` | 判空 | 替代 `!= nullptr` |

**推荐的调试习惯**：每写完一个函数，先在 `DigCell` 里加一行 `UE_LOG`，把格子坐标和剩余血量打出来。肉眼在视口里根本看不出 25cm 的差别，日志能。

---

## 7. 十个你一定会踩的坑

1. **`UPROPERTY` 忘了写** → 指针被 GC 清空，表现为"运行几秒后随机崩溃"。
2. **`BeginPlay` 忘了 `Super::BeginPlay()`** → 父类逻辑不执行，症状千奇百怪。
3. **构造函数里调 `GetWorld()`** → 返回 nullptr，崩。
4. **改 C++ 默认值，蓝图里不生效** → 因为 BP 子类早就把旧值存进 .uasset 了。要么在 BP 里点"重置"，要么改 CDO。
5. **Live Coding 状态下加 `UPROPERTY`** → 编译报错或运行时内存错乱。改结构必须关编辑器。
6. **`RemoveInstance` 之后索引全乱** → 就是 `DigSite.cpp` 里那个大坑，所以必须 `RebuildInstanceMapping()`。
7. **`Cast<T>()` 失败返回 nullptr 不报错** → 后面一行解引用就崩。`Cast` 之后永远判空。
8. **`FVector` 是 double** → 老代码里的 `float` 会隐式转换，`%f` 打印可能不对。
9. **`for (auto X : Array)` 忘了 `&`** → 改的是副本，数组没变。`TryRevealArtifacts` 里必须用 `FDigBuriedArtifact&`。
10. **在编辑器里改了 `GridSize` 但场景没变** → `OnConstruction` 没被调用（比如你用 `SpawnActor` 生成且没设置 `bRunConstructionScriptOnDrag`），需要手动调 `RebuildSite()`。

---

## 8. 五天节奏（按你的项目排）

| 天 | 做什么 | 产出 |
|---|---|---|
| D1 | 读第 1、2 节；打开 `.generated.h` 看 UHT 产物；把 `DigSite.h` 从头到尾读一遍，说出每个宏的作用 | 能解释"为什么 GridSize 用 FDigCellCoord 而不是 FIntVector" |
| D2 | 写 4 个坐标函数（`LinearIndex`/`IsValidCell`/`GetCellCenterLocal`/`LocalToCell`）+ `RebuildSite` | 编辑器里拖一个 ADigSite 进场景，能看到 8×8×6 的土块 |
| D3 | 写 `ApplyDig` + `DigCell` + `IsCellExposed` + `RemoveBlockInstance` + `RebuildInstanceMapping` | 能挖，敲 3 下碎一块，挖掉的地方凹下去 |
| D4 | 写 `DigInteractionComponent` 的三个函数，接到角色的鼠标左键 | 站着/蹲着都能挖，能挖脚下的土 |
| D5 | 写 `TryRevealArtifacts`，在 BP 里接 `OnBlockDug` 播尘土特效 | 挖到底会露出文物 |

---

## 9. 文档与源码：怎么查最省时间

1. **官方 API Reference**：`dev.epicgames.com/documentation/en-us/unreal-engine/API`
   → 搜类名 → 看 `Syntax` 的完整签名。这是唯一权威。
2. **引擎源码就在你机器上**：`D:\Tools\Unreal\UE_5.8\Engine\Source\`
   - `Runtime\Engine\Classes\` 是 `UCLASS` 声明
   - `Runtime\Engine\Private\` 是实现
   - **VS 里对着函数按 F12 直接跳过去**——这是 UE 程序员最高频的动作，比查文档快十倍
3. **官方文档的编程章节**：`Programming with C++`、`Gameplay Framework`、`Coding Standard`
4. **UHT 产物**：`Intermediate/Build/Win64/UnrealEditor/Inc/Archaeologist/UHT/*.generated.h`
5. 书和课程（选一个就够，别囤）：
   - 《Unreal Engine 5 C++ Developer: Learn C++ and Make Video Games》(GameDev.tv) —— 最系统
   - 《Game Programming Patterns》(免费在线) —— 前 3 章，理解为什么拆 Component
   - 注意：书基本都是 5.0–5.3 时代，API 有变动，报错先查官方文档

---

## 10. 每写一个函数前的 30 秒自检

- [ ] 这个函数需要 `GetWorld()` 吗？现在是构造阶段吗？（构造阶段没有 World）
- [ ] 我改的数组/指针，有没有标 `UPROPERTY`？
- [ ] 我 `Cast` 了吗？判空了吗？
- [ ] 我用的是 `Num()` 还是 `size()`？（前者）
- [ ] 我在遍历数组改元素吗？有没有漏 `&`？
- [ ] 这个函数在蓝图里会被调用吗？需要 `UFUNCTION` 吗？
- [ ] 数组下标会不会越界？`IsValidIndex` 了吗？
- [ ] 改完要不要重启编辑器？（加了 `UPROPERTY`/`UFUNCTION` → 要）
