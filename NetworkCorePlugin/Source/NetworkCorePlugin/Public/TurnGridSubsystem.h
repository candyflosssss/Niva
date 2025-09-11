// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnGridSubsystem.generated.h"



/**
 * 
 */

USTRUCT(BlueprintType)
struct FWayNodes
{
    GENERATED_BODY()

public:
    /** 路径名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WayNodes")
    FString WayName;

    /** 路径坐标 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WayNodes")
    FIntPoint WayLocation;

    /** 邻接节点列表 */
    TArray<FWayNodes> NeighborNodes;

    /** 默认构造 */
    FWayNodes()
        : WayName(TEXT(""))
        , WayLocation(0, 0)
    {
    }

    /** 带参构造 */
    FWayNodes(const FString& InName, const FIntPoint& InLocation)
        : WayName(InName)
        , WayLocation(InLocation)
    {
    }

    /** 相等比较，只基于名称和位置 */
    bool operator==(const FWayNodes& Other) const
    {
        return WayName == Other.WayName
            && WayLocation == Other.WayLocation;
    }
};

/** 全局哈希函数，用于 TMap/TSet */
FORCEINLINE uint32 GetTypeHash(const FWayNodes& Node)
{
    // 先对名称哈希，再与坐标哈希合并
    uint32 Hash = GetTypeHash(Node.WayName);
    return HashCombine(Hash, GetTypeHash(Node.WayLocation));
}

UCLASS()
class NETWORKCOREPLUGIN_API UTurnGridSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    bool ShouldCreateSubsystem(UObject* Outer) const override;

    
    /*
    * 通过静态Actor子类，和三维int坐标生成地图。
    */
	UFUNCTION(BlueprintCallable, Category = "TurnGrid")
    AActor* GridSpawnActor(TSubclassOf<AActor> ActorClass, int32 GridX, int32 GridY, int32 GridZ);

    /**
    * 生成迷宫墙体坐标
    * @param Width  逻辑房间宽度（单元格数）
    * @param Height 逻辑房间高度（单元格数）
    * @param Street  路线坐标数组
    *
    * @return 墙体的二维坐标数组（左下原点 (0,0)，右上为 (2*Width, 2*Height)）
    */
    UFUNCTION(BlueprintCallable, Category = "TurnGrid")
    TArray<FIntPoint> GenerateMazeWalls(int32 Width, int32 Height, TArray<FIntPoint>& Street);

    UFUNCTION(BlueprintCallable, Category = "TurnGrid")
    TArray<FIntPoint> FindPathAStar(const FIntPoint& Start, const FIntPoint& Goal);

	TArray<FWayNodes> FindNodes();

    UFUNCTION(BlueprintCallable, Category = "TurnGrid")
    TArray<FWayNodes> getWayNodes() { return WayNodes; };

    // 从起点计算可行走的节点
    UFUNCTION(BlueprintCallable, Category = "TurnGrid")
    TArray<FWayNodes> getWalkableWayNodes(FIntPoint Start);

    TArray<FIntPoint> Directions4 = {
    { +1,  0 },
    { -1,  0 },
    {  0, +1 },
    {  0, -1 },
    };
    int32 MapWidth;
    int32 MapHeight;


    TMap<FIntPoint, TArray<FIntPoint>> Graph;


    TArray<FIntPoint> WalkablePoint;

    TArray<FWayNodes> WayNodes;

    // 创建图
    void SetWalkableGraph();


 
    inline int static Heuristic(const FIntPoint& A, const FIntPoint& B)
    {
        return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
    }

    TArray<FString> LocationDescriptions;


};