// Fill out your copyright notice in the Description page of Project Settings.


#include "TurnGridSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Algo/RandomShuffle.h"            // Algo::Shuffle
#include "Misc/DateTime.h"           // FDateTime
#include "Math/UnrealMathUtility.h"  // FMath, FRandomStream
#include "Containers/Queue.h" // Add this include to use TPriorityQueue
#include "Kismet/GameplayStatics.h"
#include <NivaNetworkCoreSettings.h>

void UTurnGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("UMyLevelScopedSubsystem Initialized"));


    
	LocationDescriptions = GetDefault<UNivaNetworkCoreSettings>()->LocationDescriptions;

}

void UTurnGridSubsystem::Deinitialize()
{
    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("UMyLevelScopedSubsystem Deinitialized"));
}

// 重点：判断是否应该创建
bool UTurnGridSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (UWorld* World = Cast<UWorld>(Outer))
    {
        FString MapName = World->GetMapName(); // e.g. UEDPIE_0_MyLevel

        // 去掉 PIE 前缀
        MapName.RemoveFromStart(World->StreamingLevelsPrefix);

        // 限定只在指定关卡启用
        if (MapName == TEXT("TurnGridGame"))
        {
            return true;
        }
    }
    return false;
}

AActor* UTurnGridSubsystem::GridSpawnActor(TSubclassOf<AActor> ActorClass, int32 GridX, int32 GridY, int32 GridZ)
{
    if (ActorClass)
    {
        AActor* SpawnedActor = GWorld->SpawnActor<AActor>(ActorClass, { 0,0,0 }, FRotator::ZeroRotator);
        if (SpawnedActor)
        {
            // 设置网格坐标
            SpawnedActor->SetActorLocation(FVector(GridX * 100.0f, GridY * 100.0f, GridZ * 100.0f));
            return SpawnedActor;
        }
    }
    return nullptr;
}


TArray<FIntPoint> UTurnGridSubsystem::GenerateMazeWalls(int32 Width, int32 Height, TArray<FIntPoint>& Street)
{
	this->MapWidth = Width;
	this->MapHeight = Height;
    TArray<FIntPoint> WallCoords;
	// 清楚  Street
    Street.Empty();
    WalkablePoint.Empty();
    if (Width <= 0 || Height <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("MazeGen: invaild size %d x %d"), Width, Height);
        return WallCoords;
    }

    const int32 GW = 2 * Width + 1;
    const int32 GH = 2 * Height + 1;

    // true = 墙，false = 通道
    TArray<bool> bIsWall;
    bIsWall.Init(true, GW * GH);

    // 逻辑格子访问标记
    TArray<bool> Visited;
    Visited.Init(false, Width * Height);

    auto WallIndex = [&](int32 X, int32 Y) { return Y * GW + X; };
    auto CellIndex = [&](int32 CX, int32 CY) { return CY * Width + CX; };

    // 随机种子
    FRandomStream Rand(FDateTime::Now().GetTicks());

    // 显式栈
    TArray<FIntPoint> Stack;
    Stack.Push({ 0,0 });
    Visited[CellIndex(0, 0)] = true;
    bIsWall[WallIndex(1, 1)] = false;

    // UE_LOG(LogTemp, Log, TEXT("MazeGen: start, targetsize：need:(%d×%d) real:(%d×%d)"), Width, Height, GW, GH);

    while (Stack.Num() > 0)
    {
        FIntPoint Curr = Stack.Last();
        int32 CX = Curr.X, CY = Curr.Y;

        // 收集未访问邻居
        TArray<FIntPoint> Neibs;
        for (auto& D : Directions4)
        {
            int32 NX = CX + D.X, NY = CY + D.Y;
            if (NX >= 0 && NX < Width && NY >= 0 && NY < Height &&
                !Visited[CellIndex(NX, NY)])
            {
                Neibs.Add({ NX,NY });
            }
        }

        UE_LOG(LogTemp, Verbose, TEXT("MazeGen: StackDepth=%d Curr=(%d,%d) Neibs=%d"),
            Stack.Num(), CX, CY, Neibs.Num());

        if (Neibs.Num() > 0)
        {
            // 随机选
            int32 Choice = Rand.RandRange(0, Neibs.Num() - 1);
            FIntPoint Next = Neibs[Choice];
            int32 NX = Next.X, NY = Next.Y;

            // 标记访问 & 挖通房间
            Visited[CellIndex(NX, NY)] = true;
            bIsWall[WallIndex(2 * NX + 1, 2 * NY + 1)] = false;

            // 挖通中间墙
            int32 WX = CX + NX + 1;
            int32 WY = CY + NY + 1;
            bIsWall[WallIndex(WX, WY)] = false;

            Stack.Push(Next);
        }
        else
        {
            // 回溯
            Stack.Pop();
        }
    }

    // **打通入口/出口**
    // 入口：实际(1,0) ； 出口： 实际(2*W-1, 2*H)
    bIsWall[WallIndex(1, 0)] = false;
    bIsWall[WallIndex(2 * Width - 1, 2 * Height)] = false;

    // 收集墙体
    for (int32 y = 0; y < GH; ++y)
    {
        for (int32 x = 0; x < GW; ++x)
        {
            if (bIsWall[WallIndex(x, y)])
                WallCoords.Add({ x,y });
            else {
                Street.Add({ x,y });
                WalkablePoint.Add({ x,y });
            }
        }
    }

    // 分析可行走路点
    SetWalkableGraph();

    FindNodes();

    UE_LOG(LogTemp, Log, TEXT("MazeGen: successed,total=%d"), WallCoords.Num());
    return WallCoords;
}

void UTurnGridSubsystem::SetWalkableGraph()
{
    TSet<FIntPoint> WalkableSet(WalkablePoint);

    // 选 4 连通还是 8 连通，根据需求自行替换
    const TArray<FIntPoint>& Directions = Directions4;

    for (const FIntPoint& P : WalkablePoint)
    {
        TArray<FIntPoint>& Neighbors = Graph.FindOrAdd(P);
        for (const FIntPoint& Dir : Directions)
        {
            FIntPoint N = P + Dir;
            if (WalkableSet.Contains(N))
            {
                if (Neighbors.Find(N) == -1) {
                    Neighbors.Add(N);
                }
            }
        }
    }
}

TArray<FIntPoint> UTurnGridSubsystem::FindPathAStar(
    const FIntPoint& Start,
    const FIntPoint& Goal
)
{
    TSet<FIntPoint> OpenSet, ClosedSet;
    TMap<FIntPoint, int> GScore, FScore;
    TMap<FIntPoint, FIntPoint> CameFrom;

    OpenSet.Add(Start);
    GScore.Add(Start, 0);
    FScore.Add(Start, Heuristic(Start, Goal));

    while (OpenSet.Num() > 0)
    {
        // 线性扫描选出 FScore 最小节点
        FIntPoint Current;
        int BestF = TNumericLimits<int>::Max();
        for (const auto& P : OpenSet)
        {
            int F = FScore.Contains(P) ? FScore[P] : TNumericLimits<int>::Max();
            if (F < BestF)
            {
                BestF = F;
                Current = P;
            }
        }

        if (Current == Goal)
        {
            // 回溯路径
            TArray<FIntPoint> Path;
            FIntPoint P = Goal;
            while (P != Start)
            {
                Path.Add(P);
                P = CameFrom[P];
            }
            Path.Add(Start);
            Algo::Reverse(Path);
            return Path;
        }

        OpenSet.Remove(Current);
        ClosedSet.Add(Current);

        // 遍历邻居
        if (const TArray<FIntPoint>* Neighbors = Graph.Find(Current))
        {
            for (const FIntPoint& N : *Neighbors)
            {
                if (ClosedSet.Contains(N)) continue;

                int TentativeG = GScore[Current] + 1; // 每步代价 1
                if (!GScore.Contains(N) || TentativeG < GScore[N])
                {
                    CameFrom.Add(N, Current);
                    GScore.Add(N, TentativeG);
                    FScore.Add(N, TentativeG + Heuristic(N, Goal));
                    OpenSet.Add(N);
                }
            }
        }
    }

    // 无路径
    return TArray<FIntPoint>();
}


TArray<FWayNodes> UTurnGridSubsystem::FindNodes()
{
   TArray<FIntPoint> Nodes;

   // 从Graph里找到所有连接点数量不为2的点
   for (auto i : Graph) {
       if (i.Value.Num() != 2) {
           Nodes.Add(i.Key);
       }
   }

   TMap<FIntPoint, TArray<FIntPoint>> NodeMap;
   // 找到所有相连的路点
   // 通过寻路算法，找到相连的路径，再判断是否经过了其他路点，如果没有，则视为相连
   for (auto i : Nodes) {
       for (auto j : Nodes) {
           if (i == j) {
               continue;
           }
           TArray<FIntPoint> Path = FindPathAStar(i, j);
           bool bIsConnected = true;
           for (auto k : Path) {
               if (Nodes.Contains(k) && k != i && k != j ) {
                   bIsConnected = false;
                   // 看看有没有命中的
				   UE_LOG(LogTemp, Log, TEXT("FindNodes: %s -> %s: hit %s"), *i.ToString(), *j.ToString(), *k.ToString());
                   break;
               }
           }
           if (bIsConnected /*&& Path.Num() > 0*/) {
               // 如果相连，则加入到NodeMap中
               if (NodeMap.Contains(i)) {
                   NodeMap[i].Add(j);
               } else {
                   NodeMap.Add(i, { j });
               }
           }
       }
   }


   int Count = Nodes.Num();

   TArray<FString> Result;

   {
        // 1) 复制一份，保证不修改原数组
        TArray<FString> ShuffledArray = LocationDescriptions;

        // 2) 生成随机流 ―― 截取 64 位 Ticks 的低 32 位作为种子
        uint32 Seed = static_cast<uint32>(FDateTime::Now().GetTicks());
        FRandomStream RandomStream(Seed);

        // 3) 调用 UE 内置的随机打乱函数
        Algo::RandomShuffle(ShuffledArray);              // 使用 RandomShuffle
        // （如果想用自定义 RandomStream，可改成 Algo::RandomShuffle(ShuffledArray, RandomStream); 但 UE5.5 版本只接受无参版）

        // 4) 截取前 N 个元素
        Count = FMath::Clamp(Count, 0, ShuffledArray.Num());
        
        // With the following code:  
        for (int32 i = 0; i < Count && i < ShuffledArray.Num(); ++i)  
        {  
           Result.Add(ShuffledArray[i]);  
        } 
   }

   TMap<FIntPoint, FWayNodes> Location_WayNodeMap;

   for (int i = 0; i < Nodes.Num(); i++)
   {
       FWayNodes Node;
       if (Nodes[i] == FIntPoint(1, 0)) {

           FString In(TEXT("entrance"));
           Node.WayName = In;
		   Node.WayLocation = Nodes[i];
	   }
       else if (Nodes[i] == FIntPoint(MapWidth * 2-1, MapHeight * 2 )) {
           FString Out(TEXT("exit"));
           Node.WayName = Out;
		   Node.WayLocation = Nodes[i];
       }
       else {
		   Node.WayName = Result[i];
		   Node.WayLocation = Nodes[i];
       }
       Location_WayNodeMap.Add(Node.WayLocation, Node);
   }

   for (auto i : NodeMap) {
       for (auto j : i.Value) {
		   if (Location_WayNodeMap.Contains(j) && Location_WayNodeMap.Contains(i.Key)) {
               Location_WayNodeMap[i.Key].NeighborNodes.Add(Location_WayNodeMap[j]);
           }
       }
	   WayNodes.Add(Location_WayNodeMap[i.Key]);
   }


   return WayNodes;
}

TArray<FWayNodes> UTurnGridSubsystem::getWalkableWayNodes(FIntPoint Start)
{
    TArray<FWayNodes> result;

    for (auto j : WayNodes) {
        if (Start == j.WayLocation) {
            continue;
        }
        TArray<FIntPoint> Path = FindPathAStar(Start, j.WayLocation);
        bool bIsConnected = true;
        for (auto k : Path) {
            bool contain = false;
            for (auto WayNode : WayNodes) {
                if (WayNode.WayLocation == k)
                    contain = true;
            }
            if (contain && k != Start && k != j.WayLocation) {
                bIsConnected = false;
                // 看看有没有命中的
                UE_LOG(LogTemp, Log, TEXT("FindNodes: %s -> %s: hit %s"), *Start.ToString(), *j.WayLocation.ToString(), *k.ToString());
                break;
            }
        }
        if (bIsConnected /*&& Path.Num() > 0*/) {
            // 如果相连，则加入到NodeMap中
            result.Add(j);
        }
    }

    return result;
}
